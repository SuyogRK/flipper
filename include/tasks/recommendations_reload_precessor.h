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
#include <QSqlDatabase>
#include <QSharedPointer>
#include <QSet>
#include "ECacheMode.h"

#include "include/core/section.h"

namespace interfaces{
class Fanfics;
class Fandoms;
class Authors;
class PageTask;
class RecommendationLists;
}
namespace database{
class IDBWrapper;
}
class PageTask;
typedef QSharedPointer<PageTask> PageTaskPtr;


class RecommendationsProcessor : public QObject{
    Q_OBJECT
public:
    RecommendationsProcessor(QSqlDatabase db,
                                   QSharedPointer<interfaces::Fanfics> fanficInterface,
                                   QSharedPointer<interfaces::Fandoms> fandomsInterface,
                                   QSharedPointer<interfaces::Authors> authorsInterface,
                                   QSharedPointer<interfaces::RecommendationLists> recsInterface,
                                   QObject* obj = nullptr);
    virtual ~RecommendationsProcessor();
    void ReloadRecommendationsList(ECacheMode cacheMode);
    bool AddAuthorToRecommendationList(QString listName, QString authorUrl);
    bool RemoveAuthorFromRecommendationList(QString listName, QString authorUrl);
    void StageAuthorsForList(QString listName);
    void SetStagedAuthors(QList<core::AuthorPtr> list);

private:
    QSqlDatabase db;
    QList<core::AuthorPtr> stagedAuthors;
    QSharedPointer<interfaces::Fanfics> fanficsInterface;
    QSharedPointer<interfaces::Fandoms> fandomsInterface;
    QSharedPointer<interfaces::Authors> authorsInterface;
    QSharedPointer<interfaces::RecommendationLists> recsInterface;

signals:
    void resetEditorText();
    void requestProgressbar(int);
    void updateCounter(int);
    void updateInfo(QString);
};


