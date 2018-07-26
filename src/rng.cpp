#include "include/rng.h"
#include "include/Interfaces/db_interface.h"
#include <functional>
#include <random>

namespace core{
QString DefaultRNGgenerator::Get(QSharedPointer<Query> query, QString userToken, QSqlDatabase )
{
    QString where = userToken + query->str;
    for(auto bind: query->bindings)
        where += bind.key + bind.value.toString().left(30);

    //QLOG_INFO() << "RANDOM USING WHERE:" <<
    //QLOG_INFO() << "random token: " << where;
    if(!randomIdLists.contains(where))
    {
        auto idList = portableDBInterface->GetIdListForQuery(query);
        if(idList.size() == 0)
            idList.push_back("-1");
        randomIdLists[where] = idList;
    }
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 eng(rd()); // seed the generator
    auto& currentList = randomIdLists[where];
    std::uniform_int_distribution<> distr(0, currentList.size()-1); // define the range
    auto value = distr(eng);
    return currentList[value];
}
}
