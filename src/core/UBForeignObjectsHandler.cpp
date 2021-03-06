/*
* Copyright (C) 2010-2013 Groupement d'Intérêt Public pour l'Education Numérique en Afrique (GIP ENA)
*
* This file is part of Open-Sankoré.
*
* Open-Sankoré is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, version 3 of the License,
* with a specific linking exception for the OpenSSL project's
* "OpenSSL" library (or with modified versions of it that use the
* same license as the "OpenSSL" library).
*
* Open-Sankoré is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Open-Sankoré.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "UBForeignObjectsHandler.h"

#include <QtGui>
#include <QtXml>
#include "UBSettings.h"

const QString tVideo = "video";
const QString tAudio = "audio";
const QString tImage = "image";
const QString tForeignObject = "foreignObject";
const QString tTeacherGuide = "teacherGuide";
const QString tMedia = "media";

const QString aHref = "xlink:href";
const QString aType = "ub:type";
const QString aSrc = "ub:src";
const QString aMediaType = "mediaType";
const QString aRelativePath = "relativePath";

const QString vText = "text";

const QString wgtSuff = ".wgt";
const QString thumbSuff = ".png";

const QString scanDirs = "audios,images,videos,teacherGuideObjects,widgets";
const QStringList trashFilter = QStringList() << "*.swf";

class UBForeighnObjectsHandlerPrivate {
    UBForeighnObjectsHandlerPrivate(UBForeighnObjectsHandler *pq)
        : q(pq)
    {
    }

private:
    QDomDocument createDomFromSvg(const QString &svgUrl)
    {
        Q_ASSERT(QFile::exists(svgUrl));
        QString mFoldersXmlStorageName = svgUrl;

        if (QFileInfo(mFoldersXmlStorageName).exists()) {
            QDomDocument xmlDom;
            QFile inFile(mFoldersXmlStorageName);
            if (inFile.open(QIODevice::ReadOnly)) {
                QString domString(inFile.readAll());

                int errorLine = 0; int errorColumn = 0;
                QString errorStr;

                if (xmlDom.setContent(domString, &errorStr, &errorLine, &errorColumn)) {
                    return xmlDom;
                } else {
                    qDebug() << "Error reading content of " << mFoldersXmlStorageName << endl
                             << "Error:" << inFile.errorString()
                             << "Line:" << errorLine
                             << "Column:" << errorColumn;
                }
                inFile.close();
            } else {
                qDebug() << "Error reading" << mFoldersXmlStorageName << endl
                         << "Error:" << inFile.errorString();
            }
        }

        return QDomDocument();
    }

    void invokeFromText(const QString &what, const QDomElement &element)
    {
        if (what == tVideo
                || what == tAudio
                || what == tImage) {
            mediaToContainer(element);
        } else if (what == tForeignObject) {
            foreingObjectToContainer(element);
        } else if (what == tTeacherGuide) {
            teacherGuideToContainer(element);
        }
    }

    void fitIdsFromSvgDom(const QDomDocument &dom)
    {
        Q_ASSERT(!dom.isNull());

        QDomElement nextElement = dom.documentElement().firstChildElement();
        while (!nextElement.isNull()) {
            qDebug() << "Tag name of the next parsed element is" << nextElement.tagName();
            QString nextTag = nextElement.tagName();
            invokeFromText(nextTag, nextElement);
            nextElement = nextElement.nextSiblingElement();
        }
    }

    void fitIdsFromFileSystem()
    {
        QString absPrefix = mCurrentDir + "/";
        QStringList dirsList = scanDirs.split(",", QString::SkipEmptyParts);
        foreach (QString dirName, dirsList) {
            QString absPath = absPrefix + dirName;
            if (!QFile::exists(absPath)) {
                continue;
            }
            fitIdsFromDir(absPath);
        }

    }

    void fitIdsFromDir(const QString &scanDir)
    {
        QFileInfoList fileList = QDir(scanDir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        foreach (QFileInfo nInfo, fileList) {
            QString uid = strIdFrom(nInfo.fileName());
            if (uid.isNull()) {
                continue;
            }
            mPresentIdsMap.insert(uid, nInfo.absoluteFilePath());
        }
    }

    void findRedundandElements(QVector<QString> &v)
    {
        // Taking information from the physical file system
        QStringList domIds = mDomIdsMap.keys();
        QStringList presentIds = mPresentIdsMap.keys();
        v.resize(qMax(domIds.count(), presentIds.count()));
        QVector<QString>::iterator it_diff;
        it_diff=std::set_symmetric_difference(domIds.begin(), domIds.end()
                                              , presentIds.begin(), presentIds.end()
                                              , v.begin());
        v.resize(it_diff - v.begin());
    }

    bool rm_r(const QString &rmPath)
    {
        QFileInfo fi(rmPath);
        if (!fi.exists()) {
            qDebug() << rmPath << "does not exist";
            return false;
        } else if (fi.isFile()) {
            if (!QFile::remove(rmPath)) {
                qDebug() << "can't remove file" << rmPath;
                return false;
            }
            return true;
        } else if (fi.isDir()) {
            QFileInfoList fList = QDir(rmPath).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
            foreach (QFileInfo sub, fList) {
                rm_r(sub.absoluteFilePath());
            }
            if (!QDir().rmdir(rmPath)) {
                qDebug() << "can't remove dir" << rmPath;
                return false;
            }
            return true;
        }
        return false;
    }

    void cure(const QUrl &dir)
    {
        mCurrentDir = dir.toLocalFile();
        cleanTrash();

        // Gathering information from svg files
        QFileInfoList svgInfos = QDir(mCurrentDir).entryInfoList(QStringList() << "*.svg", QDir::NoDotAndDotDot | QDir::Files);
        foreach (QFileInfo svgInfo, svgInfos) {
            fitIdsFromSvgDom(createDomFromSvg(svgInfo.absoluteFilePath()));
        }

        fitIdsFromFileSystem();
        QVector<QString> deleteCandidates;
        findRedundandElements(deleteCandidates);

        foreach (QString key, deleteCandidates) {
            QString delPath = mPresentIdsMap.value(key);
            if (delPath.isNull()) {
                continue;
            } else if (delPath.endsWith(wgtSuff)) { //remove corresponding thumb
                QString thumbPath = thumbFileNameFrom(delPath);
                if (!QFile::exists(thumbPath)) {
                    continue;
                }
                rm_r(thumbPath);
            }
            rm_r(delPath);
            // Clear parent dir if empty
            QDir dir(delPath);
            dir.cdUp();
            if (dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
                dir.rmdir(dir.absolutePath());
            }
        }

        qDebug() << "Ok on cure";
    }

    void teacherGuideToContainer(const QDomElement &element)
    {
        QDomElement nMediaElement = element.firstChildElement(tMedia);
        while (!nMediaElement.isNull()) {

            QString path = nMediaElement.attribute(aRelativePath);
            if (path.isNull()) {
                continue;
            }

            QString uid = strIdFrom(path);
            if (uid.isNull()) {
                return;
            }
            mDomIdsMap.insert(uid, path);

            nMediaElement = nMediaElement.nextSiblingElement(tMedia);
        }
    }

    void mediaToContainer(const QDomElement &element)
    {
        QString path = element.attribute(aHref);
        if (path.isNull()) {
            return;
        }
        QString uid = strIdFrom(path);
        if (uid.isNull()) {
            return;
        }
        mDomIdsMap.insert(uid, path);
    }

    void foreingObjectToContainer(const QDomElement &element)
    {
        QString type = element.attribute(aType);
        if (type == vText) { // We don't have to care of the text object
            return;
        }

        QString path = element.attribute(aSrc);
        if (path.isNull()) {
            return;
        }

        QString uid = strIdFrom(path);
        if (uid.isNull()) {
            return;
        }

        mDomIdsMap.insert(uid, path);
    }

    QString strIdFrom(const QString &filePath)
    {
        if ((filePath).isEmpty()) {
            return QString();
        }

        QRegExp rx("\\{.(?!.*\\{).*\\}");
        if (rx.indexIn(filePath) == -1) {
            return QString();
        }

        return rx.cap();
    }

    QString thumbFileNameFrom(const QString &filePath)
    {
        if (filePath.isEmpty()) {
            return QString();
        }

        QString thumbPath = filePath;
        thumbPath.replace(QRegExp("[\\{\\}]"), "").replace(wgtSuff, thumbSuff);

        return thumbPath;
    }

    void cleanTrash()
    {
        QFileInfoList ifs = QDir(mCurrentDir).entryInfoList(trashFilter, QDir::NoDotAndDotDot | QDir::Files);
        foreach (QFileInfo ifo, ifs) {
            rm_r(ifo.absoluteFilePath());
        }
    }

private:
    UBForeighnObjectsHandler *q;
    QString mCurrentDir;
    QDomDocument mSvgData;
    QMap<QString, QString> mDomIdsMap;
    QMap<QString, QString> mPresentIdsMap;

    friend class UBForeighnObjectsHandler;
};

UBForeighnObjectsHandler::UBForeighnObjectsHandler()
    : d(new UBForeighnObjectsHandlerPrivate(this))
{

}

UBForeighnObjectsHandler::~UBForeighnObjectsHandler()
{
    delete d;
}

void UBForeighnObjectsHandler::cure(const QList<QUrl> &dirs)
{
    foreach (QUrl dir, dirs) {
        cure(dir);
    }
}

void UBForeighnObjectsHandler::cure(const QUrl &dir)
{
    d->cure(dir);
}
