#include "include/favholder.h"
#include "include/Interfaces/authors.h"
#include "logger/QsLog.h"
#include "include/timeutils.h"
namespace core{

void FavHolder::LoadFavourites(QSharedPointer<interfaces::Authors> authorInterface)
{
    favourites = authorInterface->LoadFullFavouritesHashset();
}

struct AuthorResult{
    int id;
    int matches;
    double ratio;
    int size;
};



RecommendationListResult FavHolder::GetMatchedFicsForFavList(QSet<int> sourceFics, QSharedPointer<RecommendationList> params)
{
    QHash<int, AuthorResult> authorsResult;
    RecommendationListResult ficResult;
    //QHash<int, int> ficResult;
    auto& favs = favourites;
    TimedAction action("Reclist Creation",[&](){
        auto it = favs.begin();
        while (it != favs.end())
        {
            auto& author = authorsResult[it.key()];
            author.id = it.key();
            author.matches = 0;
            author.ratio = 0;
            author.size = it.value().size();
            for(auto source: sourceFics)
            {
                if(it.value().contains(source))
                    author.matches = author.matches + 1;
            }
            //if(author.matches > 0)
                //QLOG_INFO() << " Author: " << it.key() << " had: " << author.matches << " matches";
            ++it;
        }
        for(auto& author: authorsResult)
        {
            if(author.matches > 0)
                author.ratio = static_cast<double>(author.size)/static_cast<double>(author.matches);
            if((author.matches >= params->minimumMatch && author.ratio <= params->pickRatio)
                    || author.matches >= params->alwaysPickAt)
            {
                for(auto fic: favourites[author.id])
                    ficResult.recommendations[fic]++;
            }
        }
    });
    action.run();
    for(auto author: authorsResult)
    {
        if((author.matches >= params->minimumMatch && author.ratio <= params->pickRatio)
                || author.matches >= params->alwaysPickAt)
        {
            ficResult.matchReport[author.matches]++;
        }
    }
    return ficResult;
}

}
