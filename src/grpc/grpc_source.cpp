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
#include "include/grpc/grpc_source.h"
#include "include/url_utils.h"
#include "include/Interfaces/fandoms.h"
#include "proto/filter.pb.h"
#include "proto/fanfic.pb.h"
#include "proto/fandom.pb.h"
#include "proto/feeder_service.pb.h"
#include "proto/feeder_service.grpc.pb.h"
#include <memory>
#include <QList>
#include <QUuid>
#include <QVector>
#include <optional>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>





static inline int GetChapterForFic(int ficId){
    return 0;
}

static inline QList<int> GetTaggedIDs(){
    return QList<int>();
}

namespace proto_converters
{
QString FS(const std::string& s)
{
    return QString::fromStdString(s);
}

std::string TS(const QString& s)
{
    return s.toStdString();
}


QDateTime DFS(const std::string& s)
{
    return QDateTime::fromString(QString::fromStdString(s), "yyyyMMdd");
}

std::string DTS(const QDateTime & date)
{
    return date.toString("yyyyMMdd").toStdString();
}



ProtoSpace::Filter StoryFilterIntoProto(const core::StoryFilter& filter,
                                        ProtoSpace::UserData* userData)
{
    // ignore fandoms intentionally not passed because likely use case can be done locally

    ProtoSpace::Filter result;
    result.set_tags_are_for_authors(filter.tagsAreUsedForAuthors);
    result.set_randomize_results(filter.randomizeResults);
    result.set_use_implied_genre(filter.useRealGenres);

    auto* basicFilters = result.mutable_basic_filters();
    basicFilters->set_website(filter.website.toStdString());
    basicFilters->set_min_words(filter.minWords);
    basicFilters->set_max_words(filter.maxWords);

    basicFilters->set_min_favourites(filter.minFavourites);
    basicFilters->set_max_fics(filter.maxFics);

    basicFilters->set_allow_unfinished(filter.allowUnfinished);
    basicFilters->set_allow_no_genre(filter.allowNoGenre);

    basicFilters->set_ensure_active(filter.ensureActive);
    basicFilters->set_ensure_completed(filter.ensureCompleted);

    result.set_filtering_mode(static_cast<ProtoSpace::Filter::FilterMode>(filter.mode));
    result.set_sort_mode(static_cast<ProtoSpace::Filter::SortMode>(filter.sortMode));
    result.set_rating(static_cast<ProtoSpace::Filter::RatingFilter>(filter.rating));
    result.set_genre_presence_include(static_cast<ProtoSpace::Filter::GenrePresence>(filter.genrePresenceForInclude));
    result.set_genre_presence_exclude(static_cast<ProtoSpace::Filter::GenrePresence>(filter.genrePresenceForExclude));
    result.set_use_this_author_only(filter.useThisAuthor);


    auto* sizeLimits = result.mutable_size_limits();
    sizeLimits->set_record_limit(filter.recordLimit);
    sizeLimits->set_record_page(filter.recordPage);

    auto* reviewBias = result.mutable_review_bias();
    reviewBias->set_bias_mode(static_cast<ProtoSpace::ReviewBias::ReviewBiasMode>(filter.reviewBias));
    reviewBias->set_bias_operator(static_cast<ProtoSpace::ReviewBias::BiasOperator>(filter.biasOperator));
    reviewBias->set_bias_ratio(filter.reviewBiasRatio);

    auto* recentPopular = result.mutable_recent_and_popular();
    recentPopular->set_fav_ratio(filter.recentAndPopularFavRatio);
    recentPopular->set_date_cutoff(filter.recentCutoff.toString("yyyyMMdd").toStdString());


    auto* contentFilter = result.mutable_content_filter();
    contentFilter->set_fandom(filter.fandom);
    contentFilter->set_include_crossovers(filter.includeCrossovers);
    contentFilter->set_crossovers_only(filter.crossoversOnly);
    contentFilter->set_other_fandoms_mode(filter.otherFandomsMode);
    contentFilter->set_use_ignored_fandoms(filter.ignoreFandoms);


    for(auto genre : filter.genreExclusion)
        contentFilter->add_genre_exclusion(genre.toStdString());
    for(auto genre : filter.genreInclusion)
        contentFilter->add_genre_inclusion(genre.toStdString());
    for(auto word : filter.wordExclusion)
        contentFilter->add_word_exclusion(word.toStdString());
    for(auto word : filter.wordInclusion)
        contentFilter->add_word_inclusion(word.toStdString());



    auto* tagFilter = result.mutable_tag_filter();
    tagFilter->set_ignore_already_tagged(filter.ignoreAlreadyTagged);

    //    auto* usertags = userData->mutable_user_tags();
    //    for(auto tag : filter.activeTags)
    //        usertags->add_searched_tags(tag);

    //    auto allTagged = GetTaggedIDs();

    //    for(auto id : allTagged)
    //        tagFilter->add_all_tagged(id);

    auto* slashFilter = result.mutable_slash_filter();
    slashFilter->set_use_slash_filter(filter.slashFilter.slashFilterEnabled);
    slashFilter->set_exclude_slash(filter.slashFilter.excludeSlash);
    slashFilter->set_include_slash(filter.slashFilter.includeSlash);
    slashFilter->set_slash_filter_level(filter.slashFilter.slashFilterLevel);
    slashFilter->set_enable_exceptions(filter.slashFilter.enableFandomExceptions);
    slashFilter->set_show_exact_level(filter.slashFilter.onlyExactLevel);
    slashFilter->set_filter_only_mature(filter.slashFilter.onlyMatureForSlash);

    for(auto exception : filter.slashFilter.fandomExceptions)
        slashFilter->add_fandom_exceptions(exception);


    auto* recommendations = result.mutable_recommendations();
    recommendations->set_list_open_mode(filter.listOpenMode);
    recommendations->set_list_for_recommendations(filter.listForRecommendations);
    recommendations->set_use_this_recommender_only(filter.useThisRecommenderOnly);
    recommendations->set_min_recommendations(filter.minRecommendations);
    recommendations->set_show_origins_in_lists(filter.showOriginsInLists);
    for(auto fic : filter.recsHash.keys())
    {
        userData->mutable_recommendation_list()->add_list_of_fics(fic);
        userData->mutable_recommendation_list()->add_list_of_matches(filter.recsHash[fic]);
    }
    return result;
}


void LocalFandomToProtoFandom(const core::Fandom& coreFandom, ProtoSpace::Fandom* protoFandom)
{
    protoFandom->set_id(coreFandom.id);
    protoFandom->set_name(TS(coreFandom.GetName()));
    protoFandom->set_website("ffn");
}

void ProtoFandomToLocalFandom(const ProtoSpace::Fandom& protoFandom, core::Fandom& coreFandom)
{
    coreFandom.id = protoFandom.id();
    coreFandom.SetName(FS(protoFandom.name()));
}




core::StoryFilter ProtoIntoStoryFilter(const ProtoSpace::Filter& filter, const ProtoSpace::UserData& userData)
{
    // ignore fandoms intentionally not passed because likely use case can be done locally

    core::StoryFilter result;
    result.randomizeResults = filter.randomize_results();
    result.useRealGenres = filter.use_implied_genre();
    result.website = FS(filter.basic_filters().website());
    result.minWords = filter.basic_filters().min_words();
    result.maxWords = filter.basic_filters().max_words();

    result.minFavourites = filter.basic_filters().min_favourites();
    result.maxFics = filter.basic_filters().max_fics();


    result.allowUnfinished = filter.basic_filters().allow_unfinished();
    result.allowNoGenre = filter.basic_filters().allow_no_genre();

    result.ensureActive = filter.basic_filters().ensure_active();
    result.ensureCompleted = filter.basic_filters().ensure_completed();

    result.mode = static_cast<core::StoryFilter::EFilterMode>(filter.filtering_mode());
    result.sortMode = static_cast<core::StoryFilter::ESortMode>(filter.sort_mode());
    result.rating = static_cast<core::StoryFilter::ERatingFilter>(filter.rating());
    result.genrePresenceForInclude = static_cast<core::StoryFilter::EGenrePresence>(filter.genre_presence_include());
    result.genrePresenceForExclude = static_cast<core::StoryFilter::EGenrePresence>(filter.genre_presence_exclude());

    result.recordLimit = filter.size_limits().record_limit();
    result.recordPage = filter.size_limits().record_page();

    result.reviewBias = static_cast<core::StoryFilter::EReviewBiasMode>(filter.review_bias().bias_mode());
    result.biasOperator = static_cast<core::StoryFilter::EBiasOperator>(filter.review_bias().bias_operator());
    result.reviewBiasRatio = filter.review_bias().bias_ratio();

    result.recentAndPopularFavRatio = filter.recent_and_popular().fav_ratio();
    result.recentCutoff = DFS(filter.recent_and_popular().date_cutoff());
    result.useThisAuthor = filter.use_this_author_only();

    result.fandom = filter.content_filter().fandom();
    result.includeCrossovers = filter.content_filter().include_crossovers();
    result.crossoversOnly = filter.content_filter().crossovers_only();
    result.otherFandomsMode = filter.content_filter().other_fandoms_mode();
    result.ignoreFandoms = filter.content_filter().use_ignored_fandoms();


    for(int i = 0; i < filter.content_filter().genre_exclusion_size(); i++)
        result.genreExclusion.push_back(FS(filter.content_filter().genre_exclusion(i)));
    for(int i = 0; i < filter.content_filter().genre_inclusion_size(); i++)
        result.genreInclusion.push_back(FS(filter.content_filter().genre_inclusion(i)));
    for(int i = 0; i < filter.content_filter().word_exclusion_size(); i++)
        result.wordExclusion.push_back(FS(filter.content_filter().word_exclusion(i)));
    for(int i = 0; i < filter.content_filter().word_inclusion_size(); i++)
        result.wordInclusion.push_back(FS(filter.content_filter().word_inclusion(i)));

    result.ignoreAlreadyTagged = filter.tag_filter().ignore_already_tagged();

    auto* userThreadData = ThreadData::GetUserData();


    userThreadData->allTaggedFics.reserve(userData.user_tags().all_tags_size());
    for(int i = 0; i < userData.user_tags().all_tags_size(); i++)
        userThreadData->allTaggedFics.insert(userData.user_tags().all_tags(i));

    userThreadData->ficIDsForActivetags.reserve(userData.user_tags().searched_tags_size());
    for(int i = 0; i < userData.user_tags().searched_tags_size(); i++)
        userThreadData->ficIDsForActivetags.insert(userData.user_tags().searched_tags(i));
    result.activeTagsCount = userThreadData->ficIDsForActivetags.size();
    result.allTagsCount = userThreadData->allTaggedFics.size();


    result.slashFilter.slashFilterEnabled = filter.slash_filter().use_slash_filter();
    result.slashFilter.excludeSlash = filter.slash_filter().exclude_slash();
    result.slashFilter.includeSlash = filter.slash_filter().include_slash();
    result.slashFilter.enableFandomExceptions = filter.slash_filter().enable_exceptions();
    result.slashFilter.slashFilterLevel = filter.slash_filter().slash_filter_level();
    result.slashFilter.onlyMatureForSlash = filter.slash_filter().filter_only_mature();
    result.slashFilter.onlyExactLevel= filter.slash_filter().show_exact_level();

    for(int i = 0; i < filter.slash_filter().fandom_exceptions_size(); i++)
        result.slashFilter.fandomExceptions.push_back(filter.slash_filter().fandom_exceptions(i));

    result.listOpenMode = filter.recommendations().list_open_mode();
    result.listForRecommendations = filter.recommendations().list_open_mode();
    result.useThisRecommenderOnly = filter.recommendations().use_this_recommender_only();
    result.minRecommendations = filter.recommendations().min_recommendations();
    result.showOriginsInLists = filter.recommendations().show_origins_in_lists();
    result.tagsAreUsedForAuthors = filter.tags_are_for_authors();

    result.ignoredFandomCount = userData.ignored_fandoms().fandom_ids_size();
    result.recommendationsCount = userData.recommendation_list().list_of_fics_size();
    for(int i = 0; i < userData.recommendation_list().list_of_fics_size(); i++)
        result.recsHash[userData.recommendation_list().list_of_fics(i)] = userData.recommendation_list().list_of_matches(i);
    for(int i = 0; i < userData.ignored_fandoms().fandom_ids_size(); i++)
        userThreadData->ignoredFandoms[userData.ignored_fandoms().fandom_ids(i)] = userData.ignored_fandoms().ignore_crossovers(i);


    return result;
}

QString RelevanceToString(float value)
{
    if(value > 0.8f)
        return "#c#";
    if(value > 0.5f)
        return "#p#";
    if(value > 0.2f)
        return "#b#";
    return "#b#";
}

QString GenreDataToString(QList<genre_stats::GenreBit> data)
{
    QStringList resultList;
    float maxValue = 0;
    for(auto genre : data)
    {
        if(genre.relevance > maxValue)
            maxValue = genre.relevance;
    }

    for(auto genre : data)
    {
        for(auto genreBit : genre.genres)
        {
            for(auto stringBit : genreBit.split(","))
            {
                if(std::abs(maxValue - genre.relevance) < 0.1f)
                    resultList+="#c#" + stringBit;
                else
                    resultList+=RelevanceToString(genre.relevance/maxValue) + stringBit;
            }
        }
    }

    QString result =  resultList.join(",");
    return result;
}


bool ProtoFicToLocalFic(const ProtoSpace::Fanfic& protoFic, core::Fic& coreFic)
{
    coreFic.isValid = protoFic.is_valid();
    if(!coreFic.isValid)
        return false;

    coreFic.id = protoFic.id();

    // I will probably disable this for now in the ui
    coreFic.atChapter = GetChapterForFic(coreFic.id);

    coreFic.complete = protoFic.complete();
    coreFic.recommendations = protoFic.recommendations();
    coreFic.wordCount = FS(protoFic.word_count());
    coreFic.chapters = FS(protoFic.chapters());
    coreFic.reviews = FS(protoFic.reviews());
    coreFic.favourites = FS(protoFic.favourites());
    coreFic.follows = FS(protoFic.follows());
    coreFic.rated = FS(protoFic.rated());
    coreFic.fandom = FS(protoFic.fandom());
    coreFic.title = FS(protoFic.title());
    coreFic.summary = FS(protoFic.summary());
    coreFic.language = FS(protoFic.language());
    coreFic.published = DFS(protoFic.published());
    coreFic.updated = DFS(protoFic.updated());
    coreFic.charactersFull = FS(protoFic.characters());

    coreFic.author = core::Author::NewAuthor();
    coreFic.author->name = FS(protoFic.author());
    coreFic.author_id = protoFic.author_id();

    for(int i = 0; i < protoFic.fandoms_size(); i++)
        coreFic.fandoms.push_back(FS(protoFic.fandoms(i)));
    for(int i = 0; i < protoFic.fandom_ids_size(); i++)
        coreFic.fandomIds.push_back(protoFic.fandom_ids(i));
    coreFic.isCrossover = coreFic.fandoms.size() > 1;

    coreFic.genreString = FS(protoFic.genres());
    coreFic.webId = protoFic.site_pack().ffn().id(); // temporary
    coreFic.ffn_id = coreFic.webId; // temporary
    coreFic.webSite = "ffn"; // temporary

    coreFic.urls["ffn"] = QString::number(coreFic.webId); // temporary
    for(auto i =0; i < protoFic.real_genres_size(); i++)
        coreFic.realGenreData.push_back({{FS(protoFic.real_genres(i).genre())}, protoFic.real_genres(i).relevance()});

    std::sort(coreFic.realGenreData.begin(),coreFic.realGenreData.end(),[](const genre_stats::GenreBit& g1,const genre_stats::GenreBit& g2){
        if(g1.genres.size() != 0 && g2.genres.size() == 0)
            return true;
        if(g2.genres.size() != 0 && g1.genres.size() == 0)
            return false;
        if(std::abs(g1.relevance - g2.relevance) < 0.1f)
            return g1.genres < g2.genres;
        return g1.relevance > g2.relevance;
    });

    coreFic.realGenreString = GenreDataToString(coreFic.realGenreData);

    coreFic.urlFFN = coreFic.urls["ffn"];
    return true;
}

bool LocalFicToProtoFic(const core::Fic& coreFic, ProtoSpace::Fanfic* protoFic)
{
    protoFic->set_is_valid(true);
    protoFic->set_id(coreFic.id);

    protoFic->set_chapters(TS(coreFic.chapters));
    protoFic->set_complete(coreFic.complete);
    protoFic->set_recommendations(coreFic.recommendations);

    protoFic->set_word_count(TS(coreFic.wordCount));
    protoFic->set_reviews(TS(coreFic.reviews));
    protoFic->set_favourites(TS(coreFic.favourites));
    protoFic->set_follows(TS(coreFic.follows));
    protoFic->set_rated(TS(coreFic.rated));
    protoFic->set_fandom(TS(coreFic.fandom));
    protoFic->set_title(TS(coreFic.title));
    protoFic->set_summary(TS(coreFic.summary));
    protoFic->set_language(TS(coreFic.language));
    protoFic->set_genres(TS(coreFic.genreString));
    protoFic->set_author(TS(coreFic.author->name));
    protoFic->set_author_id(coreFic.author_id);

    protoFic->set_published(DTS(coreFic.published));
    protoFic->set_updated(DTS(coreFic.updated));
    protoFic->set_characters(TS(coreFic.charactersFull));


    for(auto fandom : coreFic.fandoms)
        protoFic->add_fandoms(TS(fandom));
    for(auto fandom : coreFic.fandomIds)
        protoFic->add_fandom_ids(fandom);

    for(auto realGenre : coreFic.realGenreData)
    {
        auto* genreData =  protoFic->add_real_genres();
        genreData->set_genre(TS(realGenre.genres.join(",")));
        genreData->set_relevance(realGenre.relevance);
    }


    protoFic->mutable_site_pack()->mutable_ffn()->set_id(coreFic.ffn_id);

    return true;
}

bool FavListProtoToLocal(const ProtoSpace::FavListDetails &protoStats, core::FicSectionStats &stats)
{
    stats.isValid = protoStats.is_valid();
    if(!stats.isValid)
        return false;

    stats.favourites = protoStats.fic_count();
    stats.ficWordCount = protoStats.word_count();
    stats.averageWordsPerChapter = protoStats.average_words_per_chapter();
    stats.averageLength = protoStats.average_wordcount();
    stats.fandomsDiversity = protoStats.fandom_diversity();
    stats.explorerFactor = protoStats.explorer_rating();
    stats.megaExplorerFactor = protoStats.mega_explorer_rating();
    stats.crossoverFactor = protoStats.crossover_rating();
    stats.unfinishedFactor = protoStats.unfinished_rating();
    stats.genreDiversityFactor = protoStats.genre_diversity_rating();
    stats.moodUniformity = protoStats.mood_uniformity_rating();

    stats.crackRatio = protoStats.crack_rating();
    stats.slashRatio = protoStats.slash_rating();
    stats.smutRatio = protoStats.smut_rating();

    stats.firstPublished = QDate::fromString(FS(protoStats.published_first()), "yyyyMMdd");
    stats.lastPublished = QDate::fromString(FS(protoStats.published_last()), "yyyyMMdd");

    stats.moodSad = protoStats.mood_rating(0);
    stats.moodNeutral = protoStats.mood_rating(1);
    stats.moodHappy = protoStats.mood_rating(2);
    stats.noInfoCount = protoStats.no_info();

    for(auto i = 0; i< protoStats.size_rating_size(); i++)
        stats.sizeFactors[i] = protoStats.size_rating(i);
    for(auto i = 0; i< protoStats.genres_size(); i++)
        stats.genreFactors[FS(protoStats.genres(i))] = protoStats.genres_percentages(i);
    for(auto i = 0; i< protoStats.fandoms_size(); i++)
        stats.fandomsConverted[protoStats.fandoms(i)] = protoStats.fandoms_counts(i);
    return true;
}

bool FavListLocalToProto(const core::FicSectionStats &stats, ProtoSpace::FavListDetails *protoStats)
{
    protoStats->set_is_valid(true);
    protoStats->set_fic_count(stats.favourites);
    protoStats->set_word_count(stats.ficWordCount);
    protoStats->set_average_wordcount(stats.averageLength);
    protoStats->set_average_words_per_chapter(stats.averageWordsPerChapter);
    protoStats->set_fandom_diversity(stats.fandomsDiversity);
    protoStats->set_explorer_rating(stats.explorerFactor);
    protoStats->set_mega_explorer_rating(stats.megaExplorerFactor);
    protoStats->set_crossover_rating(stats.crossoverFactor);
    protoStats->set_unfinished_rating(stats.unfinishedFactor);
    protoStats->set_genre_diversity_rating(stats.genreDiversityFactor);
    protoStats->set_mood_uniformity_rating(stats.moodUniformity);


    protoStats->set_crack_rating(stats.crackRatio);
    protoStats->set_slash_rating(stats.slashRatio);
    protoStats->set_smut_rating(stats.smutRatio);

    protoStats->set_published_first(stats.firstPublished.toString("yyyyMMdd").toStdString());
    protoStats->set_published_last(stats.lastPublished.toString("yyyyMMdd").toStdString());
    protoStats->add_mood_rating(stats.moodSad);
    protoStats->add_mood_rating(stats.moodNeutral);
    protoStats->add_mood_rating(stats.moodHappy);

    for(auto size: stats.sizeFactors.keys())
        protoStats->add_size_rating(stats.sizeFactors[size]);

    for(auto genre: stats.genreFactors.keys())
    {
        protoStats->add_genres(TS(genre));
        protoStats->add_genres_percentages(stats.genreFactors[genre]);
    }

    for(auto fandom: stats.fandomsConverted.keys())
    {
        protoStats->add_fandoms(fandom);
        protoStats->add_fandoms_counts(stats.fandomsConverted[fandom]);
    }
    return true;
}
}

class FicSourceGRPCImpl{
    //    friend class GrpcServiceBase;
public:
    FicSourceGRPCImpl(QString connectionString, int deadline)
        :stub_(ProtoSpace::Feeder::NewStub(grpc::CreateChannel(connectionString.toStdString(), grpc::InsecureChannelCredentials()))),
          deadline(deadline)
    {
    }
    ServerStatus GetStatus();
    bool GetInternalIDsForFics(QVector<core::IdPack> * ficList);
    bool GetFFNIDsForFics(QVector<core::IdPack> * ficList);
    void FetchData(core::StoryFilter filter, QVector<core::Fic> * fics);
    int GetFicCount(core::StoryFilter filter);
    bool GetFandomListFromServer(int lastFandomID, QVector<core::Fandom>* fandoms);
    bool GetRecommendationListFromServer(RecommendationListGRPC& recList);
    void ProcessStandardError(grpc::Status status);
    core::FicSectionStats GetStatsForFicList(QVector<core::IdPack> ficList);

    std::unique_ptr<ProtoSpace::Feeder::Stub> stub_;
    QString error;
    bool hasErrors = false;
    int deadline = 60;
    QString userToken;
    UserData userData;
};
#define TO_STR2(x) #x
#define STRINGIFY(x) TO_STR2(x)
ServerStatus FicSourceGRPCImpl::GetStatus()
{
    ServerStatus serverStatus;
    grpc::ClientContext context;
    ProtoSpace::StatusRequest task;

    QScopedPointer<ProtoSpace::StatusResponse> response (new ProtoSpace::StatusResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    auto* controls = task.mutable_controls();
    controls->set_user_token(proto_converters::TS(userToken));

    grpc::Status status = stub_->GetStatus(&context, task, response.data());

    ProcessStandardError(status);
    if(hasErrors)
    {
        serverStatus.error = error;
        return serverStatus;
    }
    serverStatus.isValid = true;
    serverStatus.dbAttached = response->database_attached();
    QString dbUpdate = proto_converters::FS(response->last_database_update());
    serverStatus.lastDBUpdate = QDateTime::fromString("yyyyMMdd hhmm");
    serverStatus.motd = proto_converters::FS(response->message_of_the_day());
    serverStatus.messageRequired = response->need_to_show_motd();
    int ownProtocolVersion = QString(STRINGIFY(PROTOCOL_VERSION)).toInt();
    serverStatus.protocolVersionMismatch = ownProtocolVersion != response->protocol_version();
    return serverStatus;
}

bool FicSourceGRPCImpl::GetInternalIDsForFics(QVector<core::IdPack> * ficList){
    grpc::ClientContext context;

    ProtoSpace::FicIdRequest task;

    if(!ficList->size())
        return true;


    QScopedPointer<ProtoSpace::FicIdResponse> response (new ProtoSpace::FicIdResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    auto* controls = task.mutable_controls();
    controls->set_user_token(proto_converters::TS(userToken));

    for(core::IdPack& fic : *ficList)
    {
        task.mutable_ids()->add_db_ids(fic.db);
        task.mutable_ids()->add_ffn_ids(fic.ffn);
    }

    grpc::Status status = stub_->GetDBFicIDS(&context, task, response.data());

    ProcessStandardError(status);

    for(int i = 0; i < response->ids().db_ids_size(); i++)
    {
        (*ficList)[i].db = response->ids().db_ids(i);
        (*ficList)[i].ffn = response->ids().ffn_ids(i);
    }
    return true;
}

bool FicSourceGRPCImpl::GetFFNIDsForFics(QVector<core::IdPack> *ficList)
{
    grpc::ClientContext context;

    ProtoSpace::FicIdRequest task;

    if(!ficList->size())
        return true;

    QScopedPointer<ProtoSpace::FicIdResponse> response (new ProtoSpace::FicIdResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    auto* controls = task.mutable_controls();
    controls->set_user_token(proto_converters::TS(userToken));

    for(core::IdPack& fic : *ficList)
    {
        task.mutable_ids()->add_db_ids(fic.db);
        task.mutable_ids()->add_ffn_ids(fic.ffn);
    }

    grpc::Status status = stub_->GetFFNFicIDS(&context, task, response.data());

    ProcessStandardError(status);

    for(int i = 0; i < response->ids().db_ids_size(); i++)
    {
        (*ficList)[i].db = response->ids().db_ids(i);
        (*ficList)[i].ffn = response->ids().ffn_ids(i);
    }
    return true;
}

void FicSourceGRPCImpl::FetchData(core::StoryFilter filter, QVector<core::Fic> * fics)
{
    grpc::ClientContext context;

    ProtoSpace::SearchTask task;

    ProtoSpace::Filter protoFilter;
    auto* userData = task.mutable_user_data();
    protoFilter = proto_converters::StoryFilterIntoProto(filter, userData);
    task.set_allocated_filter(&protoFilter);

    QScopedPointer<ProtoSpace::SearchResponse> response (new ProtoSpace::SearchResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    auto* controls = task.mutable_controls();
    controls->set_user_token(proto_converters::TS(userToken));


    auto* tags = userData->mutable_user_tags();

    for(auto tag : this->userData.allTaggedFics)
        tags->add_all_tags(tag);
    for(auto tag : this->userData.ficIDsForActivetags)
        tags->add_searched_tags(tag);

    auto* ignoredFandoms = userData->mutable_ignored_fandoms();
    for(auto key: this->userData.ignoredFandoms.keys())
    {
        ignoredFandoms->add_fandom_ids(key);
        ignoredFandoms->add_ignore_crossovers(this->userData.ignoredFandoms[key]);
    }

    grpc::Status status = stub_->Search(&context, task, response.data());

    ProcessStandardError(status);

    fics->resize(static_cast<size_t>(response->fanfics_size()));
    for(int i = 0; i < response->fanfics_size(); i++)
    {
        proto_converters::ProtoFicToLocalFic(response->fanfics(i), (*fics)[static_cast<size_t>(i)]);
    }

    task.release_filter();
}

int FicSourceGRPCImpl::GetFicCount(core::StoryFilter filter)
{
    grpc::ClientContext context;

    ProtoSpace::FicCountTask task;

    ProtoSpace::Filter protoFilter;
    auto* userData = task.mutable_user_data();
    protoFilter = proto_converters::StoryFilterIntoProto(filter, userData);
    task.set_allocated_filter(&protoFilter);

    auto* controls = task.mutable_controls();
    controls->set_user_token(proto_converters::TS(userToken));

    QScopedPointer<ProtoSpace::FicCountResponse> response (new ProtoSpace::FicCountResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    auto* tags = userData->mutable_user_tags();
    for(auto tag : this->userData.allTaggedFics)
        tags->add_all_tags(tag);
    for(auto tag : this->userData.ficIDsForActivetags)
        tags->add_searched_tags(tag);

    auto* ignoredFandoms = userData->mutable_ignored_fandoms();
    for(auto key: this->userData.ignoredFandoms.keys())
    {
        ignoredFandoms->add_fandom_ids(key);
        ignoredFandoms->add_ignore_crossovers(this->userData.ignoredFandoms[key]);
    }

    grpc::Status status = stub_->GetFicCount(&context, task, response.data());

    ProcessStandardError(status);
    task.release_filter();
    int result = response->fic_count();
    return result;
}

bool FicSourceGRPCImpl::GetFandomListFromServer(int lastFandomID, QVector<core::Fandom>* fandoms)
{
    grpc::ClientContext context;

    ProtoSpace::SyncFandomListTask task;
    task.set_last_fandom_id(lastFandomID);

    QScopedPointer<ProtoSpace::SyncFandomListResponse> response (new ProtoSpace::SyncFandomListResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    task.mutable_controls()->set_user_token(proto_converters::TS(userToken));

    grpc::Status status = stub_->SyncFandomList(&context, task, response.data());

    ProcessStandardError(status);
    if(!response->needs_update())
        return false;

    fandoms->resize(response->fandoms_size());
    for(int i = 0; i < response->fandoms_size(); i++)
    {
        proto_converters::ProtoFandomToLocalFandom(response->fandoms(i), (*fandoms)[static_cast<size_t>(i)]);
    }
    return true;
}

bool FicSourceGRPCImpl::GetRecommendationListFromServer(RecommendationListGRPC& recList)
{
    grpc::ClientContext context;
    ProtoSpace::RecommendationListCreationRequest task;
    QScopedPointer<ProtoSpace::RecommendationListCreationResponse> response (new ProtoSpace::RecommendationListCreationResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    auto* ffn = task.mutable_id_packs();
    for(auto fic: recList.fics)
        ffn->add_ffn_ids(fic);
    task.set_list_name(proto_converters::TS(recList.listParams.name));
    task.set_always_pick_at(recList.listParams.alwaysPickAt);
    task.set_return_sources(true);
    task.set_min_fics_to_match(recList.listParams.minimumMatch);
    task.set_max_unmatched_to_one_matched(static_cast<int>(recList.listParams.pickRatio));
    task.set_use_weighting(recList.listParams.useWeighting);
    auto* controls = task.mutable_controls();
    controls->set_user_token(proto_converters::TS(userToken));

    grpc::Status status = stub_->RecommendationListCreation(&context, task, response.data());

    ProcessStandardError(status);
    if(!response->list().list_ready())
        return false;

    recList.fics.clear();
    recList.fics.reserve(response->list().fic_ids_size());
    recList.matchCounts.reserve(response->list().fic_ids_size());
    for(int i = 0; i < response->list().fic_ids_size(); i++)
    {
        recList.fics.push_back(response->list().fic_ids(i));
        recList.matchCounts.push_back(response->list().fic_matches(i));
    }
    auto it = response->list().match_report().begin();
    while(it != response->list().match_report().end())
    {
        recList.matchReport[it->first] = it->second;
        ++it;
    }
    return true;
}
void FicSourceGRPCImpl::ProcessStandardError(grpc::Status status)
{
    error.clear();
    if(status.ok())
    {
        hasErrors = false;
        return;
    }
    hasErrors = true;
    switch(status.error_code())
    {
    case 1:
        error+="Client application cancelled the request\n";
        break;
    case 4:
        error+="Deadline for request Exceeded\n";
        break;
    case 12:
        error+="Requested method is not implemented on the server\n";
        break;
    case 14:
        error+="Server is shutting down or is unavailable\n";
        break;
    case 2:
        error+="Server has thrown an unknown exception\n";
        break;
    case 8:
        error+="Server out of resources, or no memory on client\n";
        break;
    case 13:
        error+="Flow control violation\n";
        break;
    default:
        //intentionally empty
        break;
    }
    error+=QString::fromStdString(status.error_message());
}

core::FicSectionStats FicSourceGRPCImpl::GetStatsForFicList(QVector<core::IdPack> ficList)
{
    core::FicSectionStats result;

    grpc::ClientContext context;

    ProtoSpace::FavListDetailsRequest task;

    if(!ficList.size())
        return result;

    QScopedPointer<ProtoSpace::FavListDetailsResponse> response (new ProtoSpace::FavListDetailsResponse);
    std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(this->deadline);
    context.set_deadline(deadline);
    auto* controls = task.mutable_controls();
    controls->set_user_token(proto_converters::TS(userToken));

    for(core::IdPack& fic : ficList)
    {
        auto idPacks = task.mutable_id_packs();
        idPacks->add_ffn_ids(fic.ffn);
    }

    grpc::Status status = stub_->GetFavListDetails(&context, task, response.data());

    ProcessStandardError(status);

    if(!response->success())
        return result;

    proto_converters::FavListProtoToLocal(response->details(), result);

    return result;
}

FicSourceGRPC::FicSourceGRPC(QString connectionString,
                             QString userToken,
                             int deadline): impl(new FicSourceGRPCImpl(connectionString, deadline))
{
    impl->userToken = userToken;
}

FicSourceGRPC::~FicSourceGRPC()
{

}
void FicSourceGRPC::FetchData(core::StoryFilter filter, QVector<core::Fic> *fics)
{
    if(!impl)
        return;
    impl->userData = userData;
    impl->FetchData(filter, fics);
}

int FicSourceGRPC::GetFicCount(core::StoryFilter filter)
{
    if(!impl)
        return 0;
    impl->userData = userData;
    return impl->GetFicCount(filter);
}

bool FicSourceGRPC::GetFandomListFromServer(int lastFandomID, QVector<core::Fandom> *fandoms)
{
    if(!impl)
        return false;
    return impl->GetFandomListFromServer(lastFandomID, fandoms);
}

bool FicSourceGRPC::GetRecommendationListFromServer(RecommendationListGRPC &recList)
{
    if(!impl)
        return false;
    return impl->GetRecommendationListFromServer(recList);
}

bool FicSourceGRPC::GetInternalIDsForFics(QVector<core::IdPack> * ficList)
{
    if(!impl)
        return false;
    return impl->GetInternalIDsForFics(ficList);
}

bool FicSourceGRPC::GetFFNIDsForFics(QVector<core::IdPack> * ficList)
{
    if(!impl)
        return false;
    return impl->GetFFNIDsForFics(ficList);
}

std::optional<core::FicSectionStats> FicSourceGRPC::GetStatsForFicList(QVector<core::IdPack> ficList)
{
    if(!impl)
        return {};
    return impl->GetStatsForFicList(ficList);
}

ServerStatus FicSourceGRPC::GetStatus()
{
    if(!impl)
        return ServerStatus();
    return impl->GetStatus();
}

bool VerifyString(const std::string& s, int maxSize = 200){
    if(s.length() > maxSize)
        return false;
    return true;
}

bool VerifyInt(const int& val, int maxValue = 100000){
    if(val > maxValue)
        return false;
    return true;
}
template<typename T>
bool VerifyVectorSize(const T& vector, int maxSize = 10000){
    if(vector.size() > maxSize)
        return false;
    return true;
}

bool VerifyNotEmpty(const int& val){
    if(val == 0)
        return false;
    return true;
}

bool VerifyIDPack(const ::ProtoSpace::SiteIDPack& idPack)
{
    if(idPack.ffn_ids().size() == 0 && idPack.ao3_ids().size() == 0 && idPack.sb_ids().size() == 0 && idPack.sv_ids().size() == 0 )
        return false;
    if(idPack.ffn_ids().size() > 10000)
        return false;
    if(idPack.ao3_ids().size() > 10000)
        return false;
    if(idPack.sb_ids().size() > 10000)
        return false;
    if(idPack.sv_ids().size() > 10000)
        return false;
    return true;
}


bool VerifyRecommendationsRequest(const ProtoSpace::RecommendationListCreationRequest* request)
{
    if(request->list_name().size() > 100)
        return false;
    if(request->min_fics_to_match() <= 0 || request->min_fics_to_match() > 10000)
        return false;
    if(request->max_unmatched_to_one_matched() <= 0)
        return false;
    if(request->always_pick_at() < 1)
        return false;
    if(!VerifyIDPack(request->id_packs()))
        return false;
    return true;
}

bool VerifyFilterData(const ProtoSpace::Filter& filter, const ProtoSpace::UserData& user)
{
    if(!VerifyString(filter.basic_filters().website(), 10))
        return false;
    if(!VerifyInt(filter.size_limits().record_page()))
        return false;
    if(!VerifyInt(filter.content_filter().genre_exclusion_size(), 20))
        return false;
    if(!VerifyInt(filter.content_filter().genre_inclusion_size(), 20))
        return false;
    if(!VerifyInt(filter.content_filter().word_exclusion_size(), 50))
        return false;
    if(!VerifyInt(filter.content_filter().word_inclusion_size(), 50))
        return false;
    //    if(!VerifyInt(filter.tag_filter().all_tagged_size(), 50000))
    //        return false;
    //    if(!VerifyInt(filter.tag_filter().active_tags_size(), 50000))
    //        return false;
    if(!VerifyInt(user.recommendation_list().list_of_fics_size(), 1000000))
        return false;
    if(!VerifyInt(user.recommendation_list().list_of_matches_size(), 1000000))
        return false;
    if(!VerifyInt(filter.slash_filter().fandom_exceptions_size(), 20000))
        return false;

    //if(filter.tag_filter())

    return true;
}
