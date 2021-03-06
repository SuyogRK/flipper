/*
Flipper is a replacement search engine for fanfiction.net search results
Copyright (C) 2017-2018  Marchenko Nikolai

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#pragma once
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QScopedPointer>
#include <QSqlDatabase>
#include <QThread>
#include "GlobalHeaders/SingletonHolder.h"
#include "include/tasks/fandom_task_processor.h"
#include "include/webpage.h"
#include "ECacheMode.h"
#include <atomic>



class PageThreadWorker;







class PageGetterPrivate;
class PageManager
{
    public:
    PageManager();
    ~PageManager();
    void SetDatabase(QSqlDatabase _db);
    void SetCachedMode(bool value);
    bool GetCachedMode() const;
    WebPage GetPage(QString url, ECacheMode useCache = ECacheMode::dont_use_cache);
    void SavePageToDB(const WebPage & page);
    void SetAutomaticCacheLimit(QDate);
    void SetAutomaticCacheForCurrentDate(bool);
    void WipeOldCache();
    void WipeAllCache();
    QScopedPointer<PageGetterPrivate> d;
};

class PageThreadWorker: public QObject
{
    Q_OBJECT
public:
    PageThreadWorker(QObject* parent = nullptr);
    ~PageThreadWorker();
    virtual void timerEvent(QTimerEvent *);
    QString GetNext(QString);
    QDate GrabMinUpdate(QString text);
    void SetAutomaticCache(QDate);
    void SetAutomaticCacheForCurrentDate(bool);
    int timeout = 500;
    std::atomic<bool> working;
    QDate automaticCache;
    bool automaticCacheForCurrentDate = true;
public slots:
    void Task(QString url, QString lastUrl, QDate updateLimit, ECacheMode cacheMode, bool ignoreUpdateDate);
    void FandomTask(FandomParseTask);
    void ProcessBunchOfFandomUrls(QStringList urls,
                                  QDate stopAt,
                                  ECacheMode cacheMode,
                                  QStringList& failedPages);
    void TaskList(QStringList urls, ECacheMode cacheMode);
signals:
    void pageResult(PageResult);
};

BIND_TO_SELF_SINGLE(PageManager);
