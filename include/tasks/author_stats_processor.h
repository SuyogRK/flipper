/*Flipper is a replacement search engine for fanfiction.net search results
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
along with this program.  If not, see <http://www.gnu.org/licenses/>*/
#pragma once
#include <QObject>
#include <QDate>
#include <QSharedPointer>
#include "ECacheMode.h"
#include "include/pageconsumer.h"
#include "include/environment.h"
#include "include/core/section.h"

namespace interfaces{
class Fanfics;
class Fandoms;
class Authors;
class PageTask;
}
namespace database{
class IDBWrapper;
}
class PageTask;
typedef QSharedPointer<PageTask> PageTaskPtr;


class AuthorStatsProcessor: public PageConsumer{
Q_OBJECT
public:
    AuthorStatsProcessor(QSqlDatabase db,
                        QSharedPointer<interfaces::Fanfics> fanficInterface,
                        QSharedPointer<interfaces::Fandoms> fandomsInterface,
                        QSharedPointer<interfaces::Authors> authorsInterface,
                        QObject* obj = nullptr);
//    void Run(FandomParseTask task);
//    void Run(PageTaskPtr task);
//    PageTaskPtr CreatePageTaskFromFandoms(QList<core::FandomPtr> fandoms,
//                                          QString prototype,
//                                                      QString taskComment,
//                                                      ECacheMode cacheMode,
//                                                      bool allowCacheRefresh,
//                                                      ForcedFandomUpdateDate forcedDate = ForcedFandomUpdateDate());


    void ReprocessAllAuthorsStats(ECacheMode cacheMode);


private:
    QSqlDatabase db;
    QSharedPointer<interfaces::Fanfics> fanficsInterface;
    QSharedPointer<interfaces::Fandoms> fandomsInterface;
    QSharedPointer<interfaces::Authors> authorsInterface;
    QSharedPointer<interfaces::PageTask> pageInterface;
    QSharedPointer<database::IDBWrapper> dbInterface;
signals:
    void requestProgressbar(int);
    void updateCounter(int);
    void updateInfo(QString);
};
