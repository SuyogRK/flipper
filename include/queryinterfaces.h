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
#include <QString>
#include <QVariantHash>
#include <QSharedPointer>
#include <QSqlDatabase>
#include "storyfilter.h"
//#include "Interfaces/db_interface.h"
namespace database { class IDBWrapper; }
namespace core {
struct Query
{
    void Clear(){ str.clear(); bindings.clear();}
    QString str;
    QVariantHash bindings;
};

struct IRNGGenerator{
    virtual ~IRNGGenerator(){}
    virtual QString Get(QSharedPointer<Query>, QSqlDatabase db)  = 0;
};

class IQueryBuilder
{
public:
    virtual ~IQueryBuilder(){}
    virtual QSharedPointer<Query> Build(StoryFilter) = 0;
    QSharedPointer<database::IDBWrapper> portableDBInterface;
};


}