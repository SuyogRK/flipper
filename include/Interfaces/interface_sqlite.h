/*
FFSSE is a replacement search engine for fanfiction.net search results
Copyright (C) 2017  Marchenko Nikolai

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
#include "Interfaces/db_interface.h"
#include <QSharedPointer>


namespace database{
class SqliteInterface : public IDBWrapper
{
public:
    SqliteInterface();
    ~SqliteInterface();
    int GetLastIdForTable(QString tableName);
    bool PushFandomToTopOfRecent(QString fandom);
    bool RebaseFandomsToZero();
    QStringList FetchRecentFandoms();
    QDateTime GetCurrentDateTime();
    QStringList GetIdListForQuery(QSharedPointer<core::Query> query);
    bool BackupDatabase(QString dbname);
    bool ReadDbFile(QString file, QString connectionName);
    QSqlDatabase InitDatabase(QString connectionName, bool setDefault = false);

};

}