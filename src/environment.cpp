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
#include "include/environment.h"
#include "include/tagwidget.h"
#include "include/page_utils.h"
#include "include/regex_utils.h"
#include "include/Interfaces/recommendation_lists.h"
#include "include/Interfaces/fandoms.h"
#include "include/Interfaces/fanfics.h"
#include "include/Interfaces/authors.h"
#include "include/Interfaces/db_interface.h"
#include "include/Interfaces/genres.h"
#include "include/Interfaces/tags.h"
#include "include/Interfaces/data_source.h"
#include "include/grpc/grpc_source.h"
#include "include/Interfaces/pagetask_interface.h"
#include "include/Interfaces/ffn/ffn_authors.h"
#include "include/Interfaces/ffn/ffn_fanfics.h"
#include "include/db_fixers.h"
#include "include/parsers/ffn/favparser.h"
#include "include/tasks/author_cache_reprocessor.h"
#include "include/in_tag_accessor.h"
#include "include/timeutils.h"
#include "include/in_tag_accessor.h"
#include "include/url_utils.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QTextCodec>
#include <QSettings>
#include <QMessageBox>
#include <QFile>
#include <QCoreApplication>


void CoreEnvironment::InitMetatypes()
{
    qRegisterMetaType<WebPage>("WebPage");
    qRegisterMetaType<PageResult>("PageResult");
    qRegisterMetaType<ECacheMode>("ECacheMode");
    qRegisterMetaType<FandomParseTask>("FandomParseTask");
    qRegisterMetaType<FandomParseTaskResult>("FandomParseTaskResult");
}

void CoreEnvironment::LoadData()
{
    if(thinClient)
    {
        UserData userData;
        userData.allTaggedFics = interfaces.tags->GetAllTaggedFics();
        if(filter.activeTags.size() > 0)
        {
            userData.ficIDsForActivetags = interfaces.tags->GetAllTaggedFics(filter.activeTags);
            if(userData.ficIDsForActivetags.size() == 0)
            {
                QMessageBox::warning(nullptr, "Warning!", "There are no fics tagged with selected tag(s)\nAborting search.");
                return;
            }

        }
        else
        {
            if(filter.showRecSources)
            {
                auto sources = interfaces.recs->GetAllSourceFicIDs(filter.listForRecommendations);
                for(auto fic : sources)
                    userData.allTaggedFics.remove(fic);
            }
            else
            {
                auto sources = interfaces.recs->GetAllSourceFicIDs(filter.listForRecommendations);
                for(auto fic : sources)
                    userData.allTaggedFics.insert(fic);
            }
        }

        userData.ignoredFandoms = interfaces.fandoms->GetIgnoredFandomsIDs();
        ficSource->userData = userData;
    }

    QVector<int> recFics;
    filter.recsHash = interfaces.recs->GetAllFicsHash(interfaces.recs->GetCurrentRecommendationList(), filter.minRecommendations);

    QVector<core::Fic> newFanfics;
    ficSource->FetchData(filter,
                         &newFanfics);
    if(thinClient)
    {
        interfaces.fandoms->FetchFandomsForFics(&newFanfics);
        interfaces.tags->FetchTagsForFics(&newFanfics);
    }
    fanfics = newFanfics;
    currentLastFanficId = ficSource->lastFicId;
}

CoreEnvironment::CoreEnvironment(QObject *obj): QObject(obj)
{
    ReadSettings();
    rngGenerator.reset(new core::DefaultRNGgenerator);
}

void CoreEnvironment::ReadSettings()
{

}

void CoreEnvironment::WriteSettings()
{

}
static inline QString CreateConnectString(QString ip,QString port)
{
    QString server_address_proto("%1:%2");
    QString server_address = server_address_proto.arg(ip).arg(port);
    return server_address;
}

bool CoreEnvironment::Init()
{
    InitMetatypes();

    QSettings settings("settings.ini", QSettings::IniFormat);

    auto ip = settings.value("Settings/serverIp", "127.0.0.1").toString();
    auto port = settings.value("Settings/serverPort", "3055").toString();

    if(thinClient)
    {
        //interfaces.db->userToken
        interfaces.userDb->userToken = interfaces.userDb->GetUserToken();
        ficSource.reset(new FicSourceGRPC(CreateConnectString(ip, port), interfaces.userDb->userToken,  160));
        auto* grpcSource = dynamic_cast<FicSourceGRPC*>(ficSource.data());
        ServerStatus status = grpcSource->GetStatus();
        if(!status.isValid)
        {
            QString statusString = QString("The server is not responding%1");
            if(!status.error.isEmpty())
                statusString=statusString.arg("\nReason: " + status.error);
            statusString+= "\nYou could try accessing it later or ping the maintainer at ficflipper@gmail.com";
            QMessageBox::critical(nullptr, "Warning!", statusString);
            return true;
        }
        if(status.protocolVersionMismatch)
        {
            QString statusString = QString("Your client version is out of date.\nSome (or all) features may not work.\nPlease get updated binary at https://github.com/Zeks/flipper");
            QMessageBox::critical(nullptr, "Warning!", statusString);
            return true;
        }
        if(status.messageRequired)
            QMessageBox::information(nullptr, "Attention!", status.motd);

        QVector<core::Fandom> fandoms;
        grpcSource->GetFandomListFromServer(interfaces.fandoms->GetLastFandomID(), &fandoms);
        if(fandoms.size() > 0)
            interfaces.fandoms->UploadFandomsIntoDatabase(fandoms);
        auto recentFandoms = interfaces.fandoms->GetRecentFandoms();
        if(recentFandoms.size() == 0)
        {
            interfaces.fandoms->PushFandomToTopOfRecent("Naruto");
            interfaces.fandoms->PushFandomToTopOfRecent("RWBY");
            interfaces.fandoms->PushFandomToTopOfRecent("Worm");
            interfaces.fandoms->PushFandomToTopOfRecent("High School DxD");
            interfaces.fandoms->PushFandomToTopOfRecent("Harry Potter");
        }
    }
    else
        ficSource.reset(new FicSourceDirect(interfaces.db));

    auto result = interfaces.tags->ReadUserTags();

    ficSource->AddFicFilter(QSharedPointer<FicFilter>(new FicFilterSlash));

    auto storedRecList = settings.value("Settings/currentList").toString();
    interfaces.recs->SetCurrentRecommendationList(interfaces.recs->GetListIdForName(storedRecList));
    interfaces.fandoms->Load();
    interfaces.recs->LoadAvailableRecommendationLists();
    interfaces.fandoms->FillFandomList(true);
    return true;
}

void CoreEnvironment::InitInterfaces()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    QSharedPointer<database::IDBWrapper> userDBInterface;
    if(thinClient)
        userDBInterface = interfaces.userDb;
    else
        userDBInterface = interfaces.db;


    interfaces.authors = QSharedPointer<interfaces::Authors> (new interfaces::FFNAuthors());
    interfaces.fanfics = QSharedPointer<interfaces::Fanfics> (new interfaces::FFNFanfics());
    interfaces.recs   = QSharedPointer<interfaces::RecommendationLists> (new interfaces::RecommendationLists());
    interfaces.fandoms = QSharedPointer<interfaces::Fandoms> (new interfaces::Fandoms());
    interfaces.tags   = QSharedPointer<interfaces::Tags> (new interfaces::Tags());
    interfaces.genres  = QSharedPointer<interfaces::Genres> (new interfaces::Genres());
    interfaces.pageTask= QSharedPointer<interfaces::PageTask> (new interfaces::PageTask());

    // probably need to change this to db accessor
    // to ensure db availability for later

    interfaces.authors->portableDBInterface = userDBInterface;
    interfaces.fanfics->authorInterface = interfaces.authors;
    interfaces.fanfics->fandomInterface = interfaces.fandoms;
    interfaces.recs->portableDBInterface = userDBInterface;
    interfaces.recs->authorInterface = interfaces.authors;
    interfaces.fandoms->portableDBInterface = userDBInterface;
    interfaces.tags->fandomInterface = interfaces.fandoms;

    //bool isOpen = interfaces.db.tags->GetDatabase().isOpen();
    interfaces.authors->db = userDBInterface->GetDatabase();
    interfaces.fanfics->db = userDBInterface->GetDatabase();
    interfaces.recs->db    = userDBInterface->GetDatabase();
    interfaces.fandoms->db = userDBInterface->GetDatabase();
    interfaces.tags->db    = userDBInterface->GetDatabase();
    interfaces.genres->db  = userDBInterface->GetDatabase();

    interfaces.pageTask->db  = interfaces.tasks->GetDatabase();

}

int CoreEnvironment::GetResultCount()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    if(thinClient)
    {
        UserData userData;
        userData.allTaggedFics = interfaces.tags->GetAllTaggedFics();
        if(filter.activeTags.size() > 0)
            userData.ficIDsForActivetags = interfaces.tags->GetAllTaggedFics(filter.activeTags);
        userData.ignoredFandoms = interfaces.fandoms->GetIgnoredFandomsIDs();
        ficSource->userData = userData;
    }
    QVector<int> recFics;
    filter.recsHash = interfaces.recs->GetAllFicsHash(interfaces.recs->GetCurrentRecommendationList(), filter.minRecommendations);

    return ficSource->GetFicCount(filter);
}
void CoreEnvironment::UseAuthorTask(PageTaskPtr task)
{
    QSqlDatabase db = QSqlDatabase::database();
    QSqlDatabase tasksDb = QSqlDatabase::database("Tasks");
    AuthorLoadProcessor authorLoader(db,
                                     tasksDb,
                                     interfaces.fanfics,
                                     interfaces.fandoms,
                                     interfaces.authors,
                                     interfaces.pageTask);
    connect(&authorLoader, &AuthorLoadProcessor::requestProgressbar, this, &CoreEnvironment::requestProgressbar);
    connect(&authorLoader, &AuthorLoadProcessor::updateCounter, this, &CoreEnvironment::updateCounter);
    connect(&authorLoader, &AuthorLoadProcessor::updateInfo, this, &CoreEnvironment::updateInfo);
    connect(&authorLoader, &AuthorLoadProcessor::resetEditorText, this, &CoreEnvironment::resetEditorText);
    authorLoader.dbInterface = interfaces.db;
    authorLoader.Run(task);
}

void CoreEnvironment::LoadMoreAuthors(QString listName, ECacheMode cacheMode)
{
    filter.mode = core::StoryFilter::filtering_in_recommendations;
    interfaces.recs->SetCurrentRecommendationList(interfaces.recs->GetListIdForName(listName));
    QStringList authorUrls = interfaces.recs->GetLinkedPagesForList(interfaces.recs->GetCurrentRecommendationList(), "ffn");
    emit requestProgressbar(authorUrls.size());
    emit updateInfo("Authors: " + QString::number(authorUrls.size()));
    QString comment = "Loading more authors from list: " + QString::number(interfaces.recs->GetCurrentRecommendationList());
    auto pageTask = page_utils::CreatePageTaskFromUrls(interfaces.pageTask,
                                                       interfaces.db->GetCurrentDateTime(),
                                                       authorUrls,
                                                       comment,
                                                       500,
                                                       3,
                                                       cacheMode,
                                                       true);

    UseAuthorTask(pageTask);
}

void CoreEnvironment::LoadAllLinkedAuthors(ECacheMode cacheMode)
{
    filter.mode = core::StoryFilter::filtering_in_recommendations;
    QStringList authorUrls;

    auto db = QSqlDatabase::database();
    auto authorInterface = QSharedPointer<interfaces::Authors> (new interfaces::FFNAuthors());
    authorInterface->db = db;
    authorInterface->portableDBInterface = interfaces.db;

    auto authors = authorInterface->GetAllUnprocessedLinkedAuthors();
    for(auto author : authors)
    {
        authorUrls.push_back("https://www.fanfiction.net/u/" + QString::number(author));
    }

    emit requestProgressbar(authorUrls.size());
    emit updateInfo("Authors: " + QString::number(authorUrls.size()));
    QString comment = "Loading all unprocessed authors";
    auto pageTask = page_utils::CreatePageTaskFromUrls(interfaces.pageTask,
                                                       interfaces.db->GetCurrentDateTime(),
                                                       authorUrls,
                                                       comment,
                                                       500,
                                                       3,
                                                       cacheMode,
                                                       true);

    UseAuthorTask(pageTask);
}

void CoreEnvironment::LoadAllLinkedAuthorsMultiFromCache()
{
    QSqlDatabase db = QSqlDatabase::database();
    AuthorCacheReprocessor reprocessor(db,
                                       interfaces.fanfics,
                                       interfaces.fandoms,
                                       interfaces.authors);
    reprocessor.ReprocessAllAuthorsStats();
}




void CoreEnvironment::UpdateAllAuthorsWith(std::function<void(QSharedPointer<core::Author>, WebPage)> updater)
{
    auto authors = interfaces.authors->GetAllAuthors("ffn");
    emit updateInfo("Authors: " + QString::number(authors.size()));
    emit requestProgressbar(authors.size());
    An<PageManager> pager;
    int counter = 0;
    for(auto author: authors)
    {
        auto webPage = pager->GetPage(author->url("ffn"), ECacheMode::use_only_cache);
        //qDebug() << "Page loaded in: " << webPage.LoadedIn();
        emit updateCounter(counter);
        updater(author, webPage);
        counter++;
    }
}


void CoreEnvironment::ReprocessAuthorNamesFromTheirPages()
{
    auto functor = [&](QSharedPointer<core::Author> author, WebPage webPage){
        //auto splittings = SplitJob(webPage.content);
        if(!author || author->id == -1)
            return;
        QString authorName = ParseAuthorNameFromFavouritePage(webPage.content);
        author->name = authorName;
        interfaces.authors->AssignNewNameForAuthor(author, authorName);
    };
    UpdateAllAuthorsWith(functor);
}



void CoreEnvironment::ProcessListIntoRecommendations(QString list)
{
    QFile data(list);
    QSqlDatabase db = QSqlDatabase::database();
    QStringList usedList;
    QVector<int> usedIdList;
    if (data.open(QFile::ReadOnly))
    {
        QTextStream in(&data);
        QSharedPointer<core::RecommendationList> params(new core::RecommendationList);
        params->name = in.readLine().split("#").at(1);
        params->minimumMatch= in.readLine().split("#").at(1).toInt();
        params->pickRatio = in.readLine().split("#").at(1).toDouble();
        params->alwaysPickAt = in.readLine().split("#").at(1).toInt();
        interfaces.recs->LoadListIntoDatabase(params);
        database::Transaction transaction(db);
        QString str;
        do{
            str = in.readLine();
            QRegExp rx("/s/(\\d+)");
            int pos = rx.indexIn(str);
            QString ficIdPart;
            if(pos != -1)
            {
                ficIdPart = rx.cap(1);
            }
            if(ficIdPart.isEmpty())
                continue;

            if(ficIdPart.toInt() <= 0)
                continue;
            auto webId = ficIdPart.toInt();

            // at the moment works only for ffn and doesnt try to determine anything else
            auto id = interfaces.fanfics->GetIDFromWebID(webId, "ffn");
            if(id == -1)
                continue;
            qDebug()<< "Settign tag: " << "generictag" << " to: " << id;
            usedList.push_back(str);
            usedIdList.push_back(id);
            interfaces.tags->SetTagForFic(id, "generictag");
        }while(!str.isEmpty());
        params->tagToUse ="generictag";
        BuildRecommendations(params, usedIdList);
        interfaces.tags->DeleteTag("generictag");
        interfaces.recs->SetFicsAsListOrigin(usedIdList, params->id);
        transaction.finalize();
        qDebug() << "using list: " << usedList;
    }
}

QVector<int> CoreEnvironment::GetSourceFicsFromFile(QString filename)
{
    //QFile data("lists/source.txt");
    QFile data(filename);
    QVector<int> sourceList;
    if (data.open(QFile::ReadOnly))
    {
        QTextStream in(&data);
        QString str;
        do{
            str = in.readLine();
            QRegExp rx("/s/(\\d+)");
            int pos = rx.indexIn(str);
            QString ficIdPart;
            if(pos != -1)
            {
                ficIdPart = rx.cap(1);
            }
            if(ficIdPart.isEmpty())
                continue;

            if(ficIdPart.toInt() <= 0)
                continue;
            auto webId = ficIdPart.toInt();

            // at the moment works only for ffn and doesnt try to determine anything else
            sourceList.push_back(webId);
        }while(!str.isEmpty());
    }
    return sourceList;
}

int CoreEnvironment::BuildRecommendationsServerFetch(QSharedPointer<core::RecommendationList> params,
                                                     QVector<int> sourceFics,
                                                     bool automaticLike)
{
    FicSourceGRPC* grpcSource = dynamic_cast<FicSourceGRPC*>(ficSource.data());
    RecommendationListGRPC list;
    list.listParams = *params;
    list.fics = sourceFics;


    database::Transaction transaction(interfaces.recs->db);
    auto listId = interfaces.recs->GetListIdForName(params->name);
    bool result = grpcSource->GetRecommendationListFromServer(list);

    QVector<core::IdPack> pack;
    pack.resize(sourceFics.size());
    int i = 0;
    for(auto source: sourceFics)
    {
        pack[i].ffn = source;
        i++;
    }
    grpcSource->GetInternalIDsForFics(&pack);
    QSet<int> sourceSet;
    sourceSet.reserve(sourceFics.size());
    for(auto id: pack)
        sourceSet.insert(id.db);

    if(!result)
    {
        QLOG_ERROR() << "list creation failed";
        return -1;
    }
    if(list.fics.size() == 0)
    {
        return -1;
    }

    interfaces.recs->DeleteList(listId);
    interfaces.recs->LoadListIntoDatabase(params);
    qDebug() << list.fics;
    interfaces.recs->LoadListFromServerIntoDatabase(params->id,
                                                    list.fics,
                                                    list.matchCounts,
                                                    sourceSet);

    interfaces.recs->UpdateFicCountInDatabase(params->id);
    interfaces.recs->SetCurrentRecommendationList(params->id);
    emit resetEditorText();
    auto keys = list.matchReport.keys();
    std::sort(keys.begin(), keys.end());
    emit updateInfo( QString("Matches: ") + QString("Times Found:") + "<br>");
    for(auto key: keys)
    {
        emit updateInfo(QString::number(key).leftJustified(11, ' ').replace(" ", "&nbsp;") + " " + QString::number(list.matchReport[key]) + "<br>");
    }
    if(automaticLike)
    {
        for(auto fic: sourceSet)
            interfaces.tags->SetTagForFic(fic, "Liked");
    }

    transaction.finalize();
    return params->id;
}

core::FicSectionStats CoreEnvironment::GetStatsForFicList(QVector<int> sourceFics)
{
    FicSourceGRPC* grpcSource = dynamic_cast<FicSourceGRPC*>(ficSource.data());
    QVector<core::IdPack> pack;
    pack.resize(sourceFics.size());
    int i = 0;
    for(auto source: sourceFics)
    {
        pack[i].ffn = source;
        i++;
    }
    auto result = grpcSource->GetStatsForFicList(pack);
    if(result.has_value())
        return *result;
    return core::FicSectionStats();
}

int CoreEnvironment::BuildRecommendationsLocalVersion(QSharedPointer<core::RecommendationList> params, bool clearAuthors)
{
    QSqlDatabase db = QSqlDatabase::database();
    database::Transaction transaction(db);

    if(clearAuthors)
        interfaces.authors->Clear();
    interfaces.authors->LoadAuthors("ffn");
    interfaces.recs->Clear();
    //fanficsInterface->ClearIndex()
    QList<int> allAuthors = interfaces.authors->GetAllAuthorIds();;
    std::sort(std::begin(allAuthors),std::end(allAuthors));
    qDebug() << "count of author ids: " << allAuthors.size();
    QList<int> filteredAuthors;
    filteredAuthors.reserve(allAuthors.size()/10);

    //QSharedPointer<core::RecommendationList> params
    auto listId = interfaces.recs->GetListIdForName(params->name);
    interfaces.recs->DeleteList(listId);
    interfaces.recs->LoadListIntoDatabase(params);
    int counter = 0;
    int alLCounter = 0;
    int authorCounter = 0;
    for(auto authorId: allAuthors)
    {
        ++authorCounter;
        if(authorCounter%10 == 0)
            QCoreApplication::processEvents();
        auto stats = interfaces.authors->GetStatsForTag(authorId, params);


        if( stats->matchesWithReference >= params->alwaysPickAt
                || (stats->matchRatio <= params->pickRatio && stats->matchesWithReference >= params->minimumMatch) )
        {
            alLCounter++;
            auto author = interfaces.authors->GetById(authorId);
            if(author)
                qDebug() << "Fit for criteria: " << author->name;
            interfaces.recs->LoadAuthorRecommendationsIntoList(authorId, params->id);
            interfaces.recs->LoadAuthorRecommendationStatsIntoDatabase(params->id, stats);
            interfaces.recs->IncrementAllValuesInListMatchingAuthorFavourites(authorId,params->id);
            filteredAuthors.push_back(authorId);
            counter++;
        }
    }

    interfaces.recs->UpdateFicCountInDatabase(params->id);
    interfaces.recs->SetCurrentRecommendationList(params->id);

    transaction.finalize();
    qDebug() << "processed authors: " << counter;
    qDebug() << "all authors: " << alLCounter;
    return params->id;
}


int CoreEnvironment::BuildRecommendations(QSharedPointer<core::RecommendationList> params,
                                          QVector<int> sourceFics,
                                          bool automaticLike,
                                          bool clearAuthors)
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    int result = -1;
    if(settings.value("Settings/thinClient").toBool())
    {
        result =  BuildRecommendationsServerFetch(params,sourceFics, automaticLike);
    }
    else
        result = BuildRecommendationsLocalVersion(params, clearAuthors);
    return result;
}

bool CoreEnvironment::ResumeUnfinishedTasks()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    if(settings.value("Settings/skipUnfinishedTasksCheck",true).toBool())
        return false;
    auto tasks = interfaces.pageTask->GetUnfinishedTasks();
    TaskList tasksToResume;
    for(auto task : tasks)
    {
        QString diagnostics;
        diagnostics+= "Unfinished task:\n";
        diagnostics+= task->taskComment + "\n";
        diagnostics+= "Started: " + task->startedAt.toString("yyyyMMdd hh:mm") + "\n";
        Log(diagnostics);
        tasksToResume.push_back(task);
    }
    if(!tasksToResume.size())
        return false;

    // later this needs to be preceded by code that routes tasks based on their type.
    // hard coding for now to make sure base functionality works
    {
        for(auto task : tasksToResume)
        {
            auto fullTask = interfaces.pageTask->GetTaskById(task->id);
            if(fullTask->type == 0)
            {
                UseAuthorTask(fullTask);
            }
            else
            {
                if(!task)
                    return false;

                QSqlDatabase db = QSqlDatabase::database();
                FandomLoadProcessor proc(db, interfaces.fanfics, interfaces.fandoms, interfaces.pageTask, interfaces.db);

                proc.Run(fullTask);

                thread_local QString status = "%1:%2\n";
                QString diagnostics;
                diagnostics+= "Finished the job:\n";
                diagnostics+= status.arg("Inserted fics").arg(fullTask->addedFics);
                diagnostics+= status.arg("Updated fics").arg(fullTask->updatedFics);
                diagnostics+= status.arg("Duplicate fics").arg(fullTask->skippedFics);
            }
        }
    }
    return true;
}

void CoreEnvironment::CreateSimilarListForGivenFic(int id, QSqlDatabase db)
{
    static bool authorsLoaded = false;
    database::Transaction transaction(db);
    QSharedPointer<core::RecommendationList> params = core::RecommendationList::NewRecList();
    params->alwaysPickAt = 1;
    params->minimumMatch = 1;
    params->name = "similar";
    params->tagToUse = "generictag";
    params->pickRatio = 9999999999;
    interfaces.tags->SetTagForFic(id, "generictag");
    BuildRecommendations(params, {id}, false, !authorsLoaded);
    interfaces.tags->DeleteTag("generictag");
    interfaces.recs->SetFicsAsListOrigin({id}, params->id);
    transaction.finalize();
}

QVector<int> CoreEnvironment::GetListSourceFFNIds(int listId)
{
    QVector<int> result;
    auto sources = interfaces.recs->GetAllSourceFicIDs(listId);
    auto* grpcSource = dynamic_cast<FicSourceGRPC*>(ficSource.data());
    QVector<core::IdPack> pack;
    pack.resize(sources.size());
    result.reserve(sources.size());
    int i = 0;
    for(auto source: sources)
    {
        pack[i].db = source;
        i++;
    }
    grpcSource->GetFFNIDsForFics(&pack);
    for(auto id : pack)
        result.push_back(id.ffn);


    return result;
}

core::AuthorPtr CoreEnvironment::LoadAuthor(QString url, QSqlDatabase db)
{
    WebPage page;
    TimedAction fetchAction("Author page fetch", [&](){
        page = env::RequestPage(url.trimmed(),  ECacheMode::dont_use_cache);
    });
    fetchAction.run(false);
    emit resetEditorText();
    FavouriteStoryParser parser(interfaces.fanfics);
    QString name = ParseAuthorNameFromFavouritePage(page.content);
    parser.authorName = name;
    parser.ProcessPage(page.url, page.content);
    emit updateInfo(parser.diagnostics.join(""));
    database::Transaction transaction(db);
    QSet<QString> fandoms;
    interfaces.authors->EnsureId(parser.recommender.author); // assuming ffn
    auto author =interfaces.authors->GetByWebID("ffn", url_utils::GetWebId(url, "ffn").toInt());
    parser.authorStats->id = author->id;
    FavouriteStoryParser::MergeStats(author,interfaces.fandoms, {parser});

    interfaces.authors->UpdateAuthorRecord(author);
    {
        interfaces.fanfics->ProcessIntoDataQueues(parser.processedStuff);
        fandoms =interfaces.fandoms->EnsureFandoms(parser.processedStuff);
        QList<core::FicRecommendation> recommendations;
        recommendations.reserve(parser.processedStuff.size());
        for(auto& section : parser.processedStuff)
            recommendations.push_back({section, author});
        interfaces.fanfics->AddRecommendations(recommendations);
        auto result =interfaces.fanfics->FlushDataQueues();
        Q_UNUSED(result);
        interfaces.fandoms->RecalculateFandomStats(fandoms.values());
    }
    transaction.finalize();
    return author;
}

QList<QSharedPointer<core::Fic>> CoreEnvironment::LoadAuthorFics(QString url)
{
    QStringList result;
    WebPage page;
    TimedAction fetchAction("Author page fetch", [&](){
        page = env::RequestPage(url.trimmed(),  ECacheMode::dont_use_cache);
    });
    fetchAction.run(false);
    emit resetEditorText();
    FavouriteStoryParser parser(interfaces.fanfics);
    QString name = ParseAuthorNameFromFavouritePage(page.content);
    parser.authorName = name;
    parser.ProcessPage(page.url, page.content);
    emit updateInfo(parser.diagnostics.join(""));
    return parser.processedStuff;
}

PageTaskPtr CoreEnvironment::LoadTrackedFandoms(ForcedFandomUpdateDate forcedDate,
                                                ECacheMode cacheMode,
                                                QString wordCutoff)
{
    interfaces.fanfics->ClearProcessedHash();
    auto fandoms = interfaces.fandoms->ListOfTrackedFandoms();
    qDebug() << "Tracked fandoms: " << fandoms;
    emit resetEditorText();

    QStringList nameList;
    for(auto fandom : fandoms)
    {
        if(!fandom)
            continue;
        nameList.push_back(fandom->GetName());
    }

    emit updateInfo(QString("Starting load of tracked %1 fandoms <br>").arg(fandoms.size()));
    auto result = ProcessFandomsAsTask(fandoms,
                                       "",
                                       true,
                                       cacheMode,
                                       wordCutoff,
                                       forcedDate);
    return result;
}

void CoreEnvironment::FillDBIDsForTags()
{
    database::Transaction transaction(interfaces.tags->db);
    auto pack = interfaces.tags->GetAllFicsThatDontHaveDBID();
    auto* grpcSource = dynamic_cast<FicSourceGRPC*>(ficSource.data());
    grpcSource->GetInternalIDsForFics(&pack);
    interfaces.tags->FillDBIDsForFics(pack);
    transaction.finalize();
}

void CoreEnvironment::UseFandomTask(PageTaskPtr task)
{
    QSqlDatabase db = QSqlDatabase::database();
    FandomLoadProcessor proc(db, interfaces.fanfics, interfaces.fandoms, interfaces.pageTask, interfaces.db);

    connect(&proc, &FandomLoadProcessor::requestProgressbar, this, &CoreEnvironment::requestProgressbar);
    connect(&proc, &FandomLoadProcessor::updateCounter, this, &CoreEnvironment::updateCounter);
    connect(&proc, &FandomLoadProcessor::updateInfo, this, &CoreEnvironment::updateInfo);
    proc.Run(task);
}

//fixes the crossover url and selects between 60 and 100k words to add to search params
static QString CreatePrototypeWithSearchParams(QString cutoffText)
{
    QString lastPart = "?&srt=1&lan=1&r=10&len=%1";
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    int lengthCutoff = cutoffText == "100k Words" ? 100 : 60;
    lastPart=lastPart.arg(lengthCutoff);
    QString resultString = "https://www.fanfiction.net%1" + lastPart;
    qDebug() << resultString;
    return resultString;
}

PageTaskPtr CoreEnvironment::ProcessFandomsAsTask(QList<core::FandomPtr> fandoms,
                                                  QString taskComment,
                                                  bool allowCacheRefresh,
                                                  ECacheMode cacheMode,
                                                  QString cutoffText,
                                                  ForcedFandomUpdateDate forcedDate)
{
    interfaces.fanfics->ClearProcessedHash();
    QSqlDatabase db = QSqlDatabase::database();
    FandomLoadProcessor proc(db, interfaces.fanfics, interfaces.fandoms, interfaces.pageTask, interfaces.db);

    connect(&proc, &FandomLoadProcessor::requestProgressbar, this, &CoreEnvironment::requestProgressbar);
    connect(&proc, &FandomLoadProcessor::updateCounter, this, &CoreEnvironment::updateCounter);
    connect(&proc, &FandomLoadProcessor::updateInfo, this, &CoreEnvironment::updateInfo);

    auto result = proc.CreatePageTaskFromFandoms(fandoms,
                                                 CreatePrototypeWithSearchParams(cutoffText),
                                                 taskComment,
                                                 cacheMode,
                                                 allowCacheRefresh,
                                                 forcedDate);

    proc.Run(result);
    return result;
}


void CoreEnvironment::Log(QString value)
{
    qDebug() << value;
}


namespace env {
WebPage RequestPage(QString pageUrl, ECacheMode cacheMode, bool autoSaveToDB)
{
    WebPage result;
    An<PageManager> pager;
    pager->SetDatabase(QSqlDatabase::database());
    result = pager->GetPage(pageUrl, cacheMode);
    if(autoSaveToDB)
        pager->SavePageToDB(result);
    return result;
}
}



