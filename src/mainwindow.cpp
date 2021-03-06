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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "GlobalHeaders/SingletonHolder.h"
#include "GlobalHeaders/simplesettings.h"
#include "GlobalHeaders/SignalBlockerWrapper.h"
#include "Interfaces/recommendation_lists.h"
#include "Interfaces/authors.h"
#include "Interfaces/fandoms.h"
#include "Interfaces/fanfics.h"
#include "Interfaces/db_interface.h"
#include "Interfaces/interface_sqlite.h"
#include "Interfaces/pagetask_interface.h"
#include "actionprogress.h"
#include "ui_actionprogress.h"
#include "pagetask.h"
#include "timeutils.h"
#include "regex_utils.h"
#include "Interfaces/tags.h"
#include "EGenres.h"
#include <QMessageBox>
#include <QReadWriteLock>
#include <QReadLocker>
#include <QRegExp>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QPair>
#include <QPoint>
#include <QStringListModel>
#include <QDesktopServices>
#include <QTextCodec>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QQuickWidget>
#include <QDebug>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QThread>
#include <QFuture>
#include <QtConcurrent>
#include <QSqlDriver>
#include <QClipboard>
#include <QMovie>
#include <QVector>
#include <QFileDialog>
#include <chrono>
#include <algorithm>
#include <math.h>

#include "genericeventfilter.h"


#include "include/parsers/ffn/favparser.h"
#include "include/parsers/ffn/fandomparser.h"
#include "include/parsers/ffn/fandomindexparser.h"
#include "include/url_utils.h"
#include "include/pure_sql.h"
#include "include/transaction.h"
#include "include/tasks/fandom_task_processor.h"
#include "include/tasks/author_task_processor.h"
#include "include/tasks/author_stats_processor.h"
#include "include/tasks/slash_task_processor.h"
#include "include/tasks/humor_task_processor.h"
#include "include/tasks/recommendations_reload_precessor.h"
#include "include/tasks/fandom_list_reload_processor.h"
#include "include/page_utils.h"
#include "include/environment.h"
#include "include/generic_utils.h"
#include "include/statistics_utils.h"


#include "Interfaces/ffn/ffn_authors.h"
#include "Interfaces/ffn/ffn_fanfics.h"
#include "Interfaces/fandoms.h"
#include "Interfaces/recommendation_lists.h"
#include "Interfaces/tags.h"
#include "Interfaces/genres.h"
template <typename T>
struct SortedBit{
    T value;
    QString name;
};
struct TaskProgressGuard
{
    TaskProgressGuard(MainWindow* w){
        w->SetWorkingStatus();
        this->w = w;
    }
    ~TaskProgressGuard(){
        if(!hasFailures)
            w->SetFinishedStatus();
        else
            w->SetFailureStatus();
    }
    bool hasFailures = false;
    MainWindow* w = nullptr;

};

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    refreshSpin(":/icons/icons/refresh_spin_lg.gif")
{
    ui->setupUi(this);
    ui->cbNormals->lineEdit()->setClearButtonEnabled(true);
    ui->leContainsWords->setClearButtonEnabled(true);
    ui->leContainsGenre->setClearButtonEnabled(true);
    ui->leNotContainsWords->setClearButtonEnabled(true);
    ui->leNotContainsGenre->setClearButtonEnabled(true);

}

bool MainWindow::Init()
{
    QSettings settings("settings.ini", QSettings::IniFormat);

    if(!env.Init())
        return false;

    this->setWindowTitle("Flipper");
    this->setAttribute(Qt::WA_QuitOnClose);


    ui->dteFavRateCut->setDate(QDate::currentDate().addDays(-366));
    ui->pbLoadDatabase->setStyleSheet("QPushButton {background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,   stop:0 rgba(179, 229, 160, 128), stop:1 rgba(98, 211, 162, 128))}"
                                      "QPushButton:hover {background-color: #9cf27b; border: 1px solid black;border-radius: 5px;}"
                                      "QPushButton {background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,   stop:0 rgba(179, 229, 160, 128), stop:1 rgba(98, 211, 162, 128))}");



    ui->wdgTagsPlaceholder->fandomsInterface = env.interfaces.fandoms;
    ui->wdgTagsPlaceholder->tagsInterface = env.interfaces.tags;
    //    tagWidgetDynamic->fandomsInterface = env.interfaces.fandoms;
    //    tagWidgetDynamic->tagsInterface = env.interfaces.tags;

    recentFandomsModel = new QStringListModel;
    ignoredFandomsModel = new QStringListModel;
    ignoredFandomsSlashFilterModel= new QStringListModel;
    recommendersModel= new QStringListModel;



    ui->edtResults->setContextMenuPolicy(Qt::CustomContextMenu);

    auto fandomList = env.interfaces.fandoms->GetFandomList(true);
    ui->cbNormals->setModel(new QStringListModel(fandomList));
    ui->cbIgnoreFandomSelector->setModel(new QStringListModel(fandomList));
    ui->cbIgnoreFandomSlashFilter->setModel(new QStringListModel(fandomList));

    actionProgress = new ActionProgress;
    pbMain = actionProgress->ui->pbMain;
    pbMain->setMinimumWidth(200);
    pbMain->setMaximumWidth(200);
    pbMain->setValue(0);
    pbMain->setTextVisible(false);

    lblCurrentOperation = new QLabel;
    ui->statusBar->addPermanentWidget(lblCurrentOperation,1);
    ui->statusBar->addPermanentWidget(actionProgress,0);

    ui->edtResults->setOpenLinks(false);
    auto showTagWidget = settings.value("Settings/showNewTagsWidget", false).toBool();
    if(!showTagWidget)
        ui->tagWidget->removeTab(1);

    recentFandomsModel->setStringList(env.interfaces.fandoms->GetRecentFandoms());
    ignoredFandomsModel->setStringList(env.interfaces.fandoms->GetIgnoredFandoms());
    ignoredFandomsSlashFilterModel->setStringList(env.interfaces.fandoms->GetIgnoredFandomsSlashFilter());
    ui->lvTrackedFandoms->setModel(recentFandomsModel);
    ui->lvIgnoredFandoms->setModel(ignoredFandomsModel);
    ui->lvExcludedFandomsSlashFilter->setModel(ignoredFandomsSlashFilterModel);


    ProcessTagsIntoGui();
    FillRecTagCombobox();



    SetupFanficTable();
    FillRecommenderListView();
    //CreatePageThreadWorker();
    callProgress = [&](int counter) {
        pbMain->setTextVisible(true);
        pbMain->setFormat("%v");
        pbMain->setValue(counter);
        pbMain->show();
    };
    callProgressText = [&](QString value) {
        ui->edtResults->insertHtml(value);
        ui->edtResults->ensureCursorVisible();
    };
    cleanupEditor = [&](void) {
        ui->edtResults->clear();
    };

    fandomMenu.addAction("Remove fandom from list", this, SLOT(OnRemoveFandomFromRecentList()));
    ignoreFandomMenu.addAction("Remove fandom from list", this, SLOT(OnRemoveFandomFromIgnoredList()));
    ignoreFandomSlashFilterMenu.addAction("Remove fandom from list", this, SLOT(OnRemoveFandomFromSlashFilterIgnoredList()));
    ui->lvTrackedFandoms->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->lvIgnoredFandoms->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->lvExcludedFandomsSlashFilter->setContextMenuPolicy(Qt::CustomContextMenu);
    //ui->edtResults->setOpenExternalLinks(true);
    ui->edtRecsContents->setReadOnly(false);
    ui->wdgDetailedRecControl->hide();
    bool thinClient = settings.value("Settings/thinClient").toBool();
    if(thinClient)
        SetClientMode();
    ResetFilterUItoDefaults();
    ReadSettings();
    //    ui->spRecsFan->setStretchFactor(0, 0);
    //    ui->spRecsFan->setStretchFactor(1, 1);
    ui->spFanIgnFan->setCollapsible(0,0);
    ui->spFanIgnFan->setCollapsible(1,0);
    ui->spFanIgnFan->setSizes({1000,0});
    ui->spRecsFan->setCollapsible(0,0);
    ui->spRecsFan->setCollapsible(1,0);
    ui->wdgDetailedRecControl->hide();
    ui->spRecsFan->setSizes({0,1000});
    ui->wdgSlashFandomExceptions->hide();
    ui->chkEnableSlashExceptions->hide();
    return true;
}

void MainWindow::InitConnections()
{
    connect(ui->pbCopyAllUrls, SIGNAL(clicked(bool)), this, SLOT(OnCopyAllUrls()));
    connect(ui->wdgTagsPlaceholder, &TagWidget::tagToggled, this, &MainWindow::OnTagToggled);
    connect(ui->wdgTagsPlaceholder, &TagWidget::refilter, [&](){
        qwFics->rootContext()->setContextProperty("ficModel", nullptr);

        if(ui->wdgTagsPlaceholder->GetSelectedTags().size() > 0)
        {
            env.filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_fics);
            if(env.filter.isValid)
                LoadData();
        }
        if(ui->wdgTagsPlaceholder->GetSelectedTags().size() == 0)
            on_pbLoadDatabase_clicked();
        ui->edtResults->setUpdatesEnabled(true);
        ui->edtResults->setReadOnly(true);
        holder->SetData(env.fanfics);
        typetableModel->OnReloadDataFromInterface();
        qwFics->rootContext()->setContextProperty("ficModel", typetableModel);
    });

    connect(ui->wdgTagsPlaceholder, &TagWidget::tagDeleted, [&](QString tag){
        ui->wdgTagsPlaceholder->OnRemoveTagFromEdit(tag);

        if(tagList.contains(tag))
        {
            env.interfaces.tags->DeleteTag(tag);
            tagList.removeAll(tag);
            qwFics->rootContext()->setContextProperty("tagModel", tagList);
        }
    });
    connect(ui->wdgTagsPlaceholder, &TagWidget::tagAdded, [&](QString tag){
        if(!tagList.contains(tag))
        {
            env.interfaces.tags->CreateTag(tag);
            tagList.append(tag);
            qwFics->rootContext()->setContextProperty("tagModel", tagList);
        }

    });
    connect(ui->wdgTagsPlaceholder, &TagWidget::dbIDRequest, this, &MainWindow::OnFillDBIdsForTags);
    connect(ui->wdgTagsPlaceholder, &TagWidget::tagReloadRequested, this, &MainWindow::OnTagReloadRequested);
    connect(ui->lvTrackedFandoms->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::OnNewSelectionInRecentList);
    //! todo currently null
    connect(ui->lvTrackedFandoms, &QListView::customContextMenuRequested, this, &MainWindow::OnFandomsContextMenu);
    connect(ui->lvIgnoredFandoms, &QListView::customContextMenuRequested, this, &MainWindow::OnIgnoredFandomsContextMenu);
    connect(ui->lvExcludedFandomsSlashFilter, &QListView::customContextMenuRequested, this, &MainWindow::OnIgnoredFandomsSlashFilterContextMenu);
    connect(ui->edtResults, &QTextBrowser::anchorClicked, this, &MainWindow::OnOpenLogUrl);
    connect(ui->edtRecsContents, &QTextBrowser::anchorClicked, this, &MainWindow::OnOpenLogUrl);

    connect(&env, &CoreEnvironment::resetEditorText, this, &MainWindow::OnResetTextEditor);
    connect(&env, &CoreEnvironment::requestProgressbar, this, &MainWindow::OnProgressBarRequested);
    connect(&env, &CoreEnvironment::updateCounter, this, &MainWindow::OnUpdatedProgressValue);
    connect(&env, &CoreEnvironment::updateInfo, this, &MainWindow::OnNewProgressString);
    ui->chkApplyLocalSlashFilter->setVisible(false);
    ui->chkOnlySlashLocal->setVisible(false);
    ui->chkInvertedSlashFilterLocal->setVisible(false);

}

void MainWindow::InitUIFromTask(PageTaskPtr task)
{
    if(!task)
        return;
    AddToProgressLog("Authors: " + QString::number(task->size));
    ReinitProgressbar(task->size);
}


#define ADD_STRING_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = value.toString(); \
    } \
    );


#define ADD_STRING_NUMBER_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data){ \
    QString temp;\
    QSettings settings("settings.ini", QSettings::IniFormat);\
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));\
    if(settings.value("Settings/commasInWordcount", false).toBool()){\
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));\
    QLocale aEnglish;\
    temp = aEnglish.toString(data->PARAM.toInt());}\
    else{ \
    temp = data->PARAM;} \
    return QVariant(temp);} \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = value.toString(); \
    } \
    ); \


#define ADD_DATE_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = value.toDateTime(); \
    } \
    ); \

#define ADD_STRING_INTEGER_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = QString::number(value.toInt()); \
    } \
    ); \

#define ADD_INTEGER_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = value.toInt(); \
    } \
    ); \

void MainWindow::SetupTableAccess()
{
    //    holder->SetColumns(QStringList() << "fandom" << "author" << "title" << "summary" << "genre" << "characters" << "rated"
    //                       << "published" << "updated" << "url" << "tags" << "wordCount" << "favourites" << "reviews" << "chapters" << "complete" << "atChapter" );
    ADD_STRING_GETSET(holder, 0, 0, fandom);
    ADD_STRING_GETSET(holder, 1, 0, author->name);
    ADD_STRING_GETSET(holder, 2, 0, title);
    ADD_STRING_GETSET(holder, 3, 0, summary);
    ADD_STRING_GETSET(holder, 4, 0, genreString);
    ADD_STRING_GETSET(holder, 5, 0, charactersFull);
    ADD_STRING_GETSET(holder, 6, 0, rated);
    ADD_DATE_GETSET(holder, 7, 0, published);
    ADD_DATE_GETSET(holder, 8, 0, updated);
    ADD_STRING_GETSET(holder, 9, 0, urlFFN);
    ADD_STRING_GETSET(holder, 10, 0, tags);
    ADD_STRING_NUMBER_GETSET(holder, 11, 0, wordCount);
    ADD_STRING_INTEGER_GETSET(holder, 12, 0, favourites);
    ADD_STRING_INTEGER_GETSET(holder, 13, 0, reviews);
    ADD_STRING_INTEGER_GETSET(holder, 14, 0, chapters);
    ADD_INTEGER_GETSET(holder, 15, 0, complete);
    ADD_INTEGER_GETSET(holder, 16, 0, atChapter);
    ADD_INTEGER_GETSET(holder, 17, 0, id);
    ADD_INTEGER_GETSET(holder, 18, 0, recommendations);
    ADD_STRING_GETSET(holder, 19, 0, realGenreString);
    ADD_INTEGER_GETSET(holder, 20, 0, author_id);


    holder->AddFlagsFunctor(
                [](const QModelIndex& index)
    {
        if(index.column() == 8)
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        Qt::ItemFlags result;
        result |= Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        return result;
    }
    );
}


void MainWindow::SetupFanficTable()
{
    holder = new TableDataListHolder<QVector, core::Fic>();
    typetableModel = new FicModel();

    SetupTableAccess();


    holder->SetColumns(QStringList() << "fandom" << "author" << "title" << "summary"
                       << "genre" << "characters" << "rated" << "published"
                       << "updated" << "url" << "tags" << "wordCount" << "favourites"
                       << "reviews" << "chapters" << "complete" << "atChapter" << "ID"
                       << "recommendations" << "realGenres" << "author_id");

    typetableInterface = QSharedPointer<TableDataInterface>(dynamic_cast<TableDataInterface*>(holder));

    typetableModel->SetInterface(typetableInterface);

    holder->SetData(env.fanfics);
    qwFics = new QQuickWidget();
    QHBoxLayout* lay = new QHBoxLayout;
    lay->addWidget(qwFics);
    ui->wdgFicviewPlaceholder->setLayout(lay);
    qwFics->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qwFics->rootContext()->setContextProperty("ficModel", typetableModel);

    env.interfaces.tags->LoadAlltags();
    tagList = env.interfaces.tags->ReadUserTags();
    qwFics->rootContext()->setContextProperty("tagModel", tagList);
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    qwFics->rootContext()->setContextProperty("urlCopyIconVisible",
                                              settings.value("Settings/urlCopyIconVisible", true).toBool());
    qwFics->rootContext()->setContextProperty("scanIconVisible",
                                              settings.value("Settings/scanIconVisible", true).toBool());
    QUrl source("qrc:/qml/ficview.qml");
    qwFics->setSource(source);

    QObject *childObject = qwFics->rootObject()->findChild<QObject*>("lvFics");

    connect(childObject, SIGNAL(chapterChanged(QVariant, QVariant)), this, SLOT(OnChapterUpdated(QVariant, QVariant)));
    connect(childObject, SIGNAL(tagAdded(QVariant, QVariant)), this, SLOT(OnTagAdd(QVariant,QVariant)));
    connect(childObject, SIGNAL(tagAddedInTagWidget(QVariant, QVariant)), this, SLOT(OnTagAddInTagWidget(QVariant,QVariant)));
    connect(childObject, SIGNAL(tagDeleted(QVariant, QVariant)), this, SLOT(OnTagRemove(QVariant,QVariant)));
    connect(childObject, SIGNAL(tagDeletedInTagWidget(QVariant, QVariant)), this, SLOT(OnTagRemoveInTagWidget(QVariant,QVariant)));
    connect(childObject, SIGNAL(urlCopyClicked(QString)), this, SLOT(OnCopyFicUrl(QString)));
    connect(childObject, SIGNAL(findSimilarClicked(QVariant)), this, SLOT(OnFindSimilarClicked(QVariant)));
    connect(childObject, SIGNAL(recommenderCopyClicked(QString)), this, SLOT(OnOpenRecommenderLinks(QString)));
    connect(childObject, SIGNAL(refilter()), this, SLOT(OnQMLRefilter()));
    connect(childObject, SIGNAL(fandomToggled(QVariant)), this, SLOT(OnQMLFandomToggled(QVariant)));
    connect(childObject, SIGNAL(authorToggled(QVariant, QVariant)), this, SLOT(OnQMLAuthorToggled(QVariant,QVariant)));
    //connect(childObject, SIGNAL(refilterClicked()), this, SLOT(on_pbLoadDatabase_clicked()));
    QObject* windowObject= qwFics->rootObject();
    connect(windowObject, SIGNAL(backClicked()), this, SLOT(OnDisplayPreviousPage()));

    connect(windowObject, SIGNAL(forwardClicked()), this, SLOT(OnDisplayNextPage()));
    connect(windowObject, SIGNAL(pageRequested(int)), this, SLOT(OnDisplayExactPage(int)));
}

void MainWindow::OnDisplayNextPage()
{
    TaskProgressGuard guard(this);
    QObject* windowObject= qwFics->rootObject();
    windowObject->setProperty("havePagesBefore", true);
    env.filter.recordPage = ++env.pageOfCurrentQuery;
    if(env.sizeOfCurrentQuery <= env.filter.recordLimit * (env.pageOfCurrentQuery))
        windowObject->setProperty("havePagesAfter", false);

    windowObject->setProperty("currentPage", env.pageOfCurrentQuery+1);
    LoadData();
    PlaceResults();
}

void MainWindow::OnDisplayPreviousPage()
{
    TaskProgressGuard guard(this);
    QObject* windowObject= qwFics->rootObject();
    windowObject->setProperty("havePagesAfter", true);
    env.filter.recordPage = --env.pageOfCurrentQuery;

    if(env.pageOfCurrentQuery == 0)
        windowObject->setProperty("havePagesBefore", false);
    windowObject->setProperty("currentPage", env.pageOfCurrentQuery+1);
    LoadData();
    PlaceResults();
}

void MainWindow::OnDisplayExactPage(int page)
{
    TaskProgressGuard guard(this);
    if(page < 0
            || page*env.filter.recordLimit > env.sizeOfCurrentQuery
            || (env.filter.recordPage+1) == page)
        return;
    QObject* windowObject= qwFics->rootObject();
    windowObject->setProperty("currentPage", page);
    windowObject->setProperty("havePagesAfter", env.sizeOfCurrentQuery > env.filter.recordLimit * page);
    page--;
    windowObject->setProperty("havePagesBefore", page > 0);


    env.filter.recordPage = page;
    LoadData();
    PlaceResults();
}

MainWindow::~MainWindow()
{
    WriteSettings();
    env.WriteSettings();
    delete ui;
}


void MainWindow::InitInterfaces()
{
    env.thinClient = true;
    env.InitInterfaces();
}

WebPage MainWindow::RequestPage(QString pageUrl, ECacheMode cacheMode, bool autoSaveToDB)
{
    QString toInsert = "<a href=\"" + pageUrl + "\"> %1 </a>";
    toInsert= toInsert.arg(pageUrl);
    ui->edtResults->append("<span>Processing url: </span>");
    if(toInsert.trimmed().isEmpty())
        toInsert=toInsert;
    ui->edtResults->insertHtml(toInsert);

    pbMain->setTextVisible(false);
    pbMain->show();

    return env::RequestPage(pageUrl, cacheMode, autoSaveToDB);
}


void MainWindow::LoadData()
{
    if(ui->cbMinWordCount->currentText().trimmed().isEmpty())
        ui->cbMinWordCount->setCurrentText("0");

    if(env.filter.recordPage == 0)
    {
        env.sizeOfCurrentQuery = GetResultCount();
        QObject* windowObject= qwFics->rootObject();
        int currentActuaLimit = ui->chkRandomizeSelection->isChecked() ? ui->sbMaxRandomFicCount->value() : env.filter.recordLimit;
        windowObject->setProperty("totalPages", env.filter.recordLimit > 0 ? (env.sizeOfCurrentQuery/currentActuaLimit) + 1 : 1);
        windowObject->setProperty("currentPage", env.filter.recordLimit > 0 ? env.filter.recordPage+1 : 1);
        windowObject->setProperty("havePagesBefore", false);
        windowObject->setProperty("havePagesAfter", env.filter.recordLimit > 0 && env.sizeOfCurrentQuery > env.filter.recordLimit);

    }
    //ui->edtResults->setOpenExternalLinks(true);
    //ui->edtResults->clear();
    //ui->edtResults->setUpdatesEnabled(false);

    env.LoadData();
    holder->SetData(env.fanfics);
//    QObject *childObject = qwFics->rootObject()->findChild<QObject*>("lvFics");
//    childObject->setProperty("authorFilterActive", false);
}

int MainWindow::GetResultCount()
{
    // for random walking size doesn't mater
    if(ui->chkRandomizeSelection->isChecked())
        return ui->sbMaxRandomFicCount->value()-1;


    return env.GetResultCount();
}



void MainWindow::ProcessTagsIntoGui()
{
    auto tagList = env.interfaces.tags->ReadUserTags();
    QList<QPair<QString, QString>> tagPairs;

    for(auto tag : tagList)
        tagPairs.push_back({ "0", tag });
    ui->wdgTagsPlaceholder->InitFromTags(-1, tagPairs);

}

void MainWindow::SetTag(int id, QString tag, bool silent)
{
    env.interfaces.tags->SetTagForFic(id, tag);
    tagList = env.interfaces.tags->ReadUserTags();
}

void MainWindow::UnsetTag(int id, QString tag)
{
    env.interfaces.tags->RemoveTagFromFic(id, tag);
    tagList = env.interfaces.tags->ReadUserTags();
}

void MainWindow::UpdateAllAuthorsWith(std::function<void(QSharedPointer<core::Author>, WebPage)> updater)
{
    TaskProgressGuard guard(this);
    env.filter.mode = core::StoryFilter::filtering_in_recommendations;

    env.ReprocessAuthorNamesFromTheirPages();

    ShutdownProgressbar();
    ui->edtResults->clear();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
}

void MainWindow::OnCopyFicUrl(QString text)
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(text);
    ui->edtResults->insertPlainText(text + "\n");

}

void MainWindow::OnCopyAllUrls()
{
    TaskProgressGuard guard(this);
    QClipboard *clipboard = QApplication::clipboard();
    QString result;
    for(int i = 0; i < typetableModel->rowCount(); i ++)
    {
        if(ui->chkInfoForLinks->isChecked())
        {
            result += typetableModel->index(i, 2).data().toString() + "\n";
        }
        result += "https://www.fanfiction.net/s/" + typetableModel->index(i, 9).data().toString() + "\n";//\n
    }
    clipboard->setText(result);
}

void MainWindow::OnDoFormattedListByFandoms()
{
    TaskProgressGuard guard(this);
    QClipboard *clipboard = QApplication::clipboard();
    QString result;
    QList<int> ficIds;
    for(int i = 0; i < typetableModel->rowCount(); i ++)
        ficIds.push_back(typetableModel->index(i, 17).data().toInt());
    QSet<QPair<QString, int>> already;
    QMap<int, QList<core::Fic*>> byFandoms;
    for(core::Fic& fic : env.fanfics)
    {
        auto* ficPtr = &fic;

        auto fandoms = ficPtr->fandomIds;
        if(fandoms.size() == 0)
        {
            auto fandom = ficPtr->fandom.trimmed();
            qDebug() << "no fandoms written for: " << "https://www.fanfiction.net/s/" + QString::number(ficPtr->webId) + ">";
        }
        for(auto fandom: fandoms)
        {
            byFandoms[fandom].push_back(ficPtr);
        }
    }
    QHash<int, QString> fandomnNames;
    fandomnNames = env.interfaces.fandoms->GetFandomNamesForIDs(byFandoms.keys());

    result += "<ul>";
    for(auto fandomKey : byFandoms.keys())
    {
        QString name = fandomnNames[fandomKey];
        if(name.trimmed().isEmpty())
            continue;
        result+= "<li><a href=\"#" + name.toLower().replace(" ","_") +"\">" + name + "</a></li>";
    }
    An<PageManager> pager;
    pager->SetDatabase(QSqlDatabase::database());
    for(auto fandomKey : byFandoms.keys())
    {
        QString name = fandomnNames[fandomKey];
        if(name.trimmed().isEmpty())
            continue;

        result+="<h4 id=\""+ name.toLower().replace(" ","_") +  "\">" + name + "</h4>";

        for(auto fic : byFandoms[fandomKey])
        {
            auto* ficPtr = fic;
            QPair<QString, int> key = {name, fic->id};

            if(already.contains(key))
                continue;
            already.insert(key);

            auto genreString = ficPtr->genreString;
            bool validGenre = true;
            if(validGenre)
            {
                result+="<a href=https://www.fanfiction.net/s/" + QString::number(ficPtr->webId) + ">" + ficPtr->title + "</a> by " + ficPtr->author->name + "<br>";
                result+=ficPtr->genreString + "<br><br>";
                QString status = "<b>Status:</b> <font color=\"%1\">%2</font>";

                if(ficPtr->complete)
                    result+=status.arg("green").arg("Complete<br>");
                else
                    result+=status.arg("red").arg("Active<br>");
                result+=ficPtr->summary + "<br><br>";
            }

        }
    }
    clipboard->setText(result);
}

void MainWindow::OnDoFormattedList()
{
    TaskProgressGuard guard(this);
    QClipboard *clipboard = QApplication::clipboard();
    QString result;
    QList<int> ficIds;
    for(int i = 0; i < typetableModel->rowCount(); i ++)
        ficIds.push_back(typetableModel->index(i, 17).data().toInt());
    QSet<QPair<QString, int>> already;
    QMap<QString, QList<int>> byFandoms;
    for(core::Fic& fic : env.fanfics)
    {
        //auto ficPtr = env.interfaces.fanfics->GetFicById(id);
        auto* ficPtr = &fic;
        auto genreString = ficPtr->genreString;
        bool validGenre = true;
        if(validGenre)
        {
            result+="<a href=https://www.fanfiction.net/s/" + QString::number(ficPtr->webId) + ">" + ficPtr->title + "</a> by " + ficPtr->author->name + "<br>";
            result+=ficPtr->genreString + "<br><br>";
            QString status = "<b>Status:</b> <font color=\"%1\">%2</font>";

            if(ficPtr->complete)
                result+=status.arg("green").arg("Complete<br>");
            else
                result+=status.arg("red").arg("Active<br>");
            result+=ficPtr->summary + "<br><br>";
        }
    }
    clipboard->setText(result);
}

void MainWindow::on_pbLoadDatabase_clicked()
{
    TaskProgressGuard guard(this);
    env.filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_fics);
    env.filter.recordPage = 0;
    env.pageOfCurrentQuery = 0;
    if(env.filter.isValid)
    {
        LoadData();
        PlaceResults();
    }
    AnalyzeCurrentFilter();
}

void MainWindow::LoadAutomaticSettingsForRecListSources(int size)
{
    if(size == 0)
        return;
    if(size == 1)
    {
        ui->leRecsMinimumMatches->setText("1");
        ui->leRecsPickRatio->setText("99999");
        ui->leRecsAlwaysPickAt->setText("1");
        return;
    }
    if(size > 1 && size <= 7)
    {
        ui->leRecsMinimumMatches->setText("4");
        ui->leRecsPickRatio->setText("70");
        ui->leRecsAlwaysPickAt->setText(QString::number(size));
        return;
    }
    if(size > 7 && size <= 20)
    {
        ui->leRecsMinimumMatches->setText("6");
        ui->leRecsPickRatio->setText("70");
        ui->leRecsAlwaysPickAt->setText(QString::number(size));
        return;
    }
    if(size > 20 && size <= 300)
    {
        ui->leRecsMinimumMatches->setText("6");
        ui->leRecsPickRatio->setText("50");
        ui->leRecsAlwaysPickAt->setText(QString::number(size));
        return;
    }
    if(size > 300)
    {
        ui->leRecsMinimumMatches->setText("6");
        ui->leRecsPickRatio->setText("40");
        ui->leRecsAlwaysPickAt->setText(QString::number(size));
        return;
    }
}

QList<QSharedPointer<core::Fic>> MainWindow::LoadFavourteLinksFromFFNProfile(QString url)
{
    QList<QSharedPointer<core::Fic>> result;
    //need to make sure it *is* FFN url
    //https://www.fanfiction.net/u/3697775/Rumour-of-an-Alchemist
    QRegularExpression rx("https://www.fanfiction.net/u/(\\d)+");
    auto match = rx.match(url);
    if(!match.hasMatch())
    {
        QMessageBox::warning(nullptr, "Warning!", "URL is not an FFN author url\nNeeeds to be a https://www.fanfiction.net/u/NUMERIC_ID");
        return result;
    }
    result = env.LoadAuthorFics(url);
    auto end = result.end();
    auto it = std::remove_if(result.begin(), result.end(), [](QSharedPointer<core::Fic> f){
            return f->ficSource == core::Fic::efs_own_works;
});
    if(it != end)
        result.erase(it, end);

    return result;
}

void MainWindow::OnQMLRefilter()
{
    on_pbLoadDatabase_clicked();
}

void MainWindow::OnQMLFandomToggled(QVariant var)
{
    auto fanficsInterface = env.interfaces.fanfics;

    int rownum = var.toInt();

    QModelIndex index = typetableModel->index(rownum, 0);
    auto data = index.data(static_cast<int>(FicModel::FandomRole)).toString();

    QStringList list = data.split("&", QString::SkipEmptyParts);
    if(!list.size())
        return;
    if(ui->cbIgnoreFandomSelector->currentText().trimmed() != list.at(0).trimmed())
        ui->cbIgnoreFandomSelector->setCurrentText(list.at(0).trimmed());
    else if(list.size() > 1)
        ui->cbIgnoreFandomSelector->setCurrentText(list.at(1).trimmed());
}

void MainWindow::OnQMLAuthorToggled(QVariant var, QVariant active)
{
    auto fanficsInterface = env.interfaces.fanfics;

    int rownum = var.toInt();

    QModelIndex index = typetableModel->index(rownum, 0);
    auto data = index.data(static_cast<int>(FicModel::AuthorIdRole)).toInt();
    if(active.toBool())
        ui->leAuthorID->setText(QString::number(data));
    else
        ui->leAuthorID->setText("");

    env.filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_fics);
    //env.filter.useThisAuthor = data;
    LoadData();

//    /QObject *childObject = qwFics->rootObject()->findChild<QObject*>("lvFics");
    //childObject->setProperty("authorFilterActive", true);
    AnalyzeCurrentFilter();

}


void MainWindow::ReadSettings()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));

    ui->wdgCustomActions->setVisible(settings.value("Settings/showCustomActions", false).toBool());
    //ui->chkGroupFandoms->setVisible(settings.value("Settings/showListCreation", false).toBool());
    //ui->chkInfoForLinks->setVisible(settings.value("Settings/showListCreation", false).toBool());


    ui->cbRecGroup->setVisible(settings.value("Settings/showRecListSelector", false).toBool());

    ui->cbNormals->setCurrentText(settings.value("Settings/normals", "").toString());

    QSettings uiSettings("ui.ini", QSettings::IniFormat);
    uiSettings.setIniCodec(QTextCodec::codecForName("UTF-8"));

    ui->cbNormals->setCurrentText(uiSettings.value("Settings/normals", "").toString());

    ui->cbMaxWordCount->setCurrentText(uiSettings.value("Settings/maxWordCount", "").toString());
    ui->cbMinWordCount->setCurrentText(uiSettings.value("Settings/minWordCount", 100000).toString());

    ui->leContainsGenre->setText(uiSettings.value("Settings/plusGenre", "").toString());
    ui->leNotContainsGenre->setText(uiSettings.value("Settings/minusGenre", "").toString());
    ui->leNotContainsWords->setText(uiSettings.value("Settings/minusWords", "").toString());
    ui->leContainsWords->setText(uiSettings.value("Settings/plusWords", "").toString());

    //ui->chkHeartProfile->setChecked(uiSettings.value("Settings/chkHeartProfile", false).toBool());
    ui->chkGenrePlus->setChecked(uiSettings.value("Settings/chkGenrePlus", false).toBool());
    ui->chkGenreMinus->setChecked(uiSettings.value("Settings/chkGenreMinus", false).toBool());
    ui->chkWordsPlus->setChecked(uiSettings.value("Settings/chkWordsPlus", false).toBool());
    ui->chkWordsMinus->setChecked(uiSettings.value("Settings/chkWordsMinus", false).toBool());

    ui->chkActive->setChecked(uiSettings.value("Settings/active", false).toBool());
    ui->chkShowUnfinished->setChecked(uiSettings.value("Settings/showUnfinished", false).toBool());
    ui->chkNoGenre->setChecked(uiSettings.value("Settings/chkNoGenre", false).toBool());
    ui->chkComplete->setChecked(uiSettings.value("Settings/completed", false).toBool());
    ui->chkShowSources->setChecked(uiSettings.value("Settings/chkShowSources", false).toBool());
    ui->chkSearchWithinList->setChecked(uiSettings.value("Settings/chkSearchWithinList", false).toBool());
    ui->chkAutomaticLike->setChecked(uiSettings.value("Settings/chkAutomaticLike", false).toBool());
    ui->chkFaveLimitActivated->setChecked(uiSettings.value("Settings/chkFaveLimitActivated", false).toBool());
    ui->spMain->restoreState(uiSettings.value("Settings/spMain", false).toByteArray());
    ui->spDebug->restoreState(uiSettings.value("Settings/spDebug", false).toByteArray());
    ui->spSourceAnalysis->restoreState(uiSettings.value("Settings/spSourceAnalysis", false).toByteArray());
    ui->cbSortMode->setCurrentText(uiSettings.value("Settings/currentSortFilter", "Update Date").toString());
    ui->cbBiasFavor->setCurrentText(uiSettings.value("Settings/biasMode", "None").toString());
    ui->cbBiasOperator->setCurrentText(uiSettings.value("Settings/biasOperator", "<").toString());
    ui->cbRecsAlgo->setCurrentText(uiSettings.value("Settings/cbRecsAlgo", "Weighted").toString());
    ui->leBiasValue->setText(uiSettings.value("Settings/biasValue", "2.5").toString());

    ui->sbMinimumListMatches->setValue(uiSettings.value("Settings/sbMinimumListMatches", "0").toInt());
    ui->sbMinimumFavourites->setValue(uiSettings.value("Settings/sbMinimumFavourites", "0").toInt());

    ui->chkGenreUseImplied->setChecked(uiSettings.value("Settings/chkGenreUseImplied", false).toBool());
    ui->cbGenrePresenceTypeInclude->setCurrentText(uiSettings.value("Settings/cbGenrePresenceTypeInclude", "").toString());
    ui->cbGenrePresenceTypeExclude->setCurrentText(uiSettings.value("Settings/cbGenrePresenceTypeExclude", "").toString());
    ui->cbFicRating->setCurrentText(uiSettings.value("Settings/cbFicRating", "<").toString());

    ui->chkStrongMOnly->setChecked(uiSettings.value("Settings/chkStrongMOnly", false).toBool());
    ui->cbSlashFilterAggressiveness->setCurrentText(uiSettings.value("Settings/cbSlashFilterAggressiveness", "").toString());

    DetectGenreSearchState();
    DetectSlashSearchState();

    if(!uiSettings.value("Settings/appsize").toSize().isNull())
        this->resize(uiSettings.value("Settings/appsize").toSize());
    if(!uiSettings.value("Settings/position").toPoint().isNull())
        this->move(uiSettings.value("Settings/position").toPoint());
}

void MainWindow::WriteSettings()
{
    QSettings settings("ui.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    settings.setValue("Settings/minWordCount", ui->cbMinWordCount->currentText());
    settings.setValue("Settings/maxWordCount", ui->cbMaxWordCount->currentText());

    settings.setValue("Settings/sbMinimumListMatches", ui->sbMinimumListMatches->value());
    settings.setValue("Settings/sbMinimumFavourites", ui->sbMinimumFavourites->value());
    settings.setValue("Settings/chkFaveLimitActivated", ui->chkFaveLimitActivated->isChecked());
    settings.setValue("Settings/chkAutomaticLike", ui->chkAutomaticLike->isChecked());

    settings.setValue("Settings/normals", GetCurrentFandomName());
    settings.setValue("Settings/plusGenre", ui->leContainsGenre->text());
    settings.setValue("Settings/minusGenre", ui->leNotContainsGenre->text());
    settings.setValue("Settings/plusWords", ui->leContainsWords->text());
    settings.setValue("Settings/minusWords", ui->leNotContainsWords->text());


    //settings.setValue("Settings/chkHeartProfile", ui->chkHeartProfile->isChecked());
    settings.setValue("Settings/chkGenrePlus", ui->chkGenrePlus->isChecked());
    settings.setValue("Settings/chkGenreMinus", ui->chkGenreMinus->isChecked());
    settings.setValue("Settings/chkWordsPlus", ui->chkWordsPlus->isChecked());
    settings.setValue("Settings/chkWordsMinus", ui->chkWordsMinus->isChecked());

    settings.setValue("Settings/active", ui->chkActive->isChecked());
    settings.setValue("Settings/showUnfinished", ui->chkShowUnfinished->isChecked());
    settings.setValue("Settings/chkNoGenre", ui->chkNoGenre->isChecked());
    settings.setValue("Settings/completed", ui->chkComplete->isChecked());
    settings.setValue("Settings/spMain", ui->spMain->saveState());
    settings.setValue("Settings/spDebug", ui->spDebug->saveState());
    settings.setValue("Settings/spSourceAnalysis", ui->spSourceAnalysis->saveState());
    settings.setValue("Settings/currentSortFilter", ui->cbSortMode->currentText());
    settings.setValue("Settings/biasMode", ui->cbBiasFavor->currentText());
    settings.setValue("Settings/biasOperator", ui->cbBiasOperator->currentText());
    settings.setValue("Settings/biasValue", ui->leBiasValue->text());
    settings.setValue("Settings/currentList", ui->cbRecGroup->currentText());
    settings.setValue("Settings/chkShowSources", ui->chkShowSources->isChecked());
    settings.setValue("Settings/chkSearchWithinList", ui->chkShowSources->isChecked());

    settings.setValue("Settings/chkGenreUseImplied", ui->chkGenreUseImplied->isChecked());
    settings.setValue("Settings/cbGenrePresenceTypeInclude", ui->cbGenrePresenceTypeInclude->currentText());
    settings.setValue("Settings/cbRecsAlgo", ui->cbRecsAlgo->currentText());
    settings.setValue("Settings/cbGenrePresenceTypeExclude", ui->cbGenrePresenceTypeExclude->currentText());
    settings.setValue("Settings/cbFicRating", ui->cbFicRating->currentText());

    settings.setValue("Settings/chkStrongMOnly", ui->chkStrongMOnly->isChecked());
    settings.setValue("Settings/cbSlashFilterAggressiveness", ui->cbSlashFilterAggressiveness->currentText());


    settings.setValue("Settings/appsize", this->size());
    settings.setValue("Settings/position", this->pos());
    settings.sync();
}

QString MainWindow::GetCurrentFandomName()
{
    return core::Fandom::ConvertName(ui->cbNormals->currentText());
}

int MainWindow::GetCurrentFandomID()
{
    return env.interfaces.fandoms->GetIDForName(core::Fandom::ConvertName(ui->cbNormals->currentText()));
    //return core::Fandom::ConvertName(ui->cbNormals->currentText());
}

void MainWindow::OnChapterUpdated(QVariant id, QVariant chapter)
{
    env.interfaces.fanfics->AssignChapter(id.toInt(), chapter.toInt());
}


void MainWindow::OnTagAdd(QVariant tag, QVariant row)
{
    int rownum = row.toInt();
    QModelIndex index = typetableModel->index(rownum, 10);
    auto data = typetableModel->data(index, 0).toString();
    data += " " + tag.toString();

    auto id = typetableModel->data(typetableModel->index(rownum, 17), 0).toInt();
    SetTag(id, tag.toString());

    typetableModel->setData(index,data,0);
    typetableModel->updateAll();
}

void MainWindow::OnTagRemove(QVariant tag, QVariant row)
{
    int rownum = row.toInt();
    QModelIndex index = typetableModel->index(row.toInt(), 10);
    auto data = typetableModel->data(index, 0).toString();
    data = data.replace(tag.toString(), "");

    auto id = typetableModel->data(typetableModel->index(rownum, 17), 0).toInt();
    UnsetTag(id, tag.toString());

    typetableModel->setData(index,data,0);
    typetableModel->updateAll();
}

void MainWindow::OnTagAddInTagWidget(QVariant tag, QVariant row)
{
    int rownum = row.toInt();
    SetTag(rownum, tag.toString());
}

void MainWindow::OnTagRemoveInTagWidget(QVariant tag, QVariant row)
{
    int rownum = row.toInt();
    UnsetTag(rownum, tag.toString());
}


void MainWindow::ReinitRecent(QString name)
{
    env.interfaces.fandoms->PushFandomToTopOfRecent(name);
    env.interfaces.fandoms->ReloadRecentFandoms();
    recentFandomsModel->setStringList(env.interfaces.fandoms->GetRecentFandoms());
}

void MainWindow::StartTaskTimer()
{
    taskTimer.setSingleShot(true);
    taskTimer.start(1000);
}

bool MainWindow::AskYesNoQuestion(QString value)
{
    QMessageBox m;
    m.setIcon(QMessageBox::Warning);
    m.setText(value);
    auto yesButton =  m.addButton("Yes", QMessageBox::AcceptRole);
    auto noButton =  m.addButton("Cancel", QMessageBox::AcceptRole);
    Q_UNUSED(noButton);
    m.exec();
    if(m.clickedButton() != yesButton)
        return false;
    return true;
}

void MainWindow::PlaceResults()
{
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    holder->SetData(env.fanfics);
    ReinitRecent(GetCurrentFandomName());
}

void MainWindow::SetWorkingStatus()
{
    refreshSpin.setScaledSize(actionProgress->ui->lblCurrentStatusIcon->size());
    refreshSpin.start();
    actionProgress->ui->lblCurrentStatusIcon->setMovie(&refreshSpin);
    actionProgress->ui->lblCurrentStatus->setText("working...");
}

void MainWindow::SetFinishedStatus()
{
    QPixmap px(":/icons/icons/ok.png");
    px = px.scaled(24, 24);
    actionProgress->ui->lblCurrentStatusIcon->setPixmap(px);
    actionProgress->ui->lblCurrentStatus->setText("Done!");
}

void MainWindow::SetFailureStatus()
{
    QPixmap px(":/icons/icons/error2.png");
    px = px.scaled(24, 24);
    actionProgress->ui->lblCurrentStatusIcon->setPixmap(px);
    actionProgress->ui->lblCurrentStatus->setText("Error!");
}


void MainWindow::OnTagToggled(QString , bool )
{
    if(ui->wdgTagsPlaceholder->GetSelectedTags().size() == 0)
        ui->chkEnableTagsFilter->setChecked(false);
    else
        ui->chkEnableTagsFilter->setChecked(true);
}

// needed to bind values to reasonable limits
void MainWindow::on_chkRandomizeSelection_clicked(bool checked)
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    auto ficLimitActive =  ui->chkRandomizeSelection->isChecked();
    int maxFicCountValue = ficLimitActive ? ui->sbMaxRandomFicCount->value()  : 0;
    if(checked && (maxFicCountValue < 1 || maxFicCountValue >50))
        ui->sbMaxRandomFicCount->setValue(settings.value("Settings/defaultRandomFicCount", 6).toInt());
}


void MainWindow::on_cbSortMode_currentTextChanged(const QString &)
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    if(ui->cbSortMode->currentText() == "Rec Count")
        ui->cbRecGroup->setVisible(settings.value("Settings/showRecListSelector", false).toBool());
}

void MainWindow::on_pbExpandPlusGenre_clicked()
{
    currentExpandedEdit = ui->leContainsGenre;
    CallExpandedWidget();
}

void MainWindow::on_pbExpandMinusGenre_clicked()
{
    currentExpandedEdit = ui->leNotContainsGenre;
    CallExpandedWidget();
}

void MainWindow::on_pbExpandPlusWords_clicked()
{
    currentExpandedEdit = ui->leContainsWords;
    CallExpandedWidget();
}

void MainWindow::on_pbExpandMinusWords_clicked()
{
    currentExpandedEdit = ui->leNotContainsWords;
    CallExpandedWidget();
}

void MainWindow::OnNewSelectionInRecentList(const QModelIndex &current, const QModelIndex &)
{
    ui->cbNormals->setCurrentText(current.data().toString());
    auto fandom = env.interfaces.fandoms->GetFandom(GetCurrentFandomName());
}

void MainWindow::CallExpandedWidget()
{
    if(!expanderWidget)
    {
        expanderWidget = new QDialog();
        expanderWidget->resize(400, 300);
        QVBoxLayout* vl = new QVBoxLayout;
        QPushButton* okButton = new QPushButton;
        okButton->setText("OK");
        edtExpander = new QTextEdit;
        vl->addWidget(edtExpander);
        vl->addWidget(okButton);
        expanderWidget->setLayout(vl);
        connect(okButton, &QPushButton::clicked, [&](){
            if(currentExpandedEdit)
                currentExpandedEdit->setText(edtExpander->toPlainText());
            expanderWidget->hide();
        });
    }
    if(currentExpandedEdit)
        edtExpander->setText(currentExpandedEdit->text());
    expanderWidget->exec();
}



//void MainWindow::CreatePageThreadWorker()
//{
//    env.worker = new PageThreadWorker;
//    env.worker->moveToThread(&env.pageThread);

//}

//void MainWindow::StartPageWorker()
//{
//    env.pageQueue.data.clear();
//    env.pageQueue.pending = true;
//    //worker->SetAutomaticCache(QDate::currentDate());
//    env.pageThread.start(QThread::HighPriority);

//}

//void MainWindow::StopPageWorker()
//{
//    env.pageThread.quit();
//}

void MainWindow::ReinitProgressbar(int maxValue)
{
    pbMain->setMaximum(maxValue);
    pbMain->setValue(0);
    pbMain->show();
}

void MainWindow::ShutdownProgressbar()
{
    pbMain->setValue(0);
    //pbMain->hide();
}

void MainWindow::AddToProgressLog(QString value)
{
    ui->edtResults->insertHtml(value);
    ui->edtResults->ensureCursorVisible();
    QCoreApplication::processEvents();
}


void MainWindow::FillRecTagCombobox()
{
    auto lists = env.interfaces.recs->GetAllRecommendationListNames();
    SilentCall(ui->cbRecGroup)->setModel(new QStringListModel(lists));
    if(lists.size() > 0)
    {
        ui->pbDeleteRecList->setEnabled(true);
        ui->pbGetSourceLinks->setEnabled(true);
    }
    else
    {
        ui->pbDeleteRecList->setEnabled(false);
        ui->pbGetSourceLinks->setEnabled(false);
    }

    QSettings settings("settings.ini", QSettings::IniFormat);
    auto storedRecList = settings.value("Settings/currentList").toString();
    qDebug() << QDir::currentPath();
    ui->cbRecGroup->setCurrentText(storedRecList);
    auto listId = env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText());
    env.interfaces.recs->SetCurrentRecommendationList(listId);
}

void MainWindow::FillRecommenderListView(bool forceRefresh)
{
    QStringList result;
    auto list = env.interfaces.recs->GetCurrentRecommendationList();
    auto allStats = env.interfaces.recs->GetAuthorStatsForList(list, forceRefresh);
    std::sort(std::begin(allStats),std::end(allStats), [](auto s1, auto s2){
        return s1->matchRatio < s2->matchRatio;
    });
    for(auto stat : allStats)
        result.push_back(stat->authorName);
    recommendersModel->setStringList(result);
}

core::AuthorPtr MainWindow::LoadAuthor(QString url)
{
    TaskProgressGuard guard(this);
    return env.LoadAuthor(url, QSqlDatabase::database());
}

void MainWindow::on_chkTrackedFandom_toggled(bool checked)
{
    env.interfaces.fandoms->SetTracked(GetCurrentFandomName(),checked);
}

ECacheMode MainWindow::GetCurrentCacheMode() const
{
    return ECacheMode::dont_use_cache;
}

void MainWindow::CreateSimilarListForGivenFic(int id)
{
    TaskProgressGuard guard(this);
    QSqlDatabase db = QSqlDatabase::database();

    QSharedPointer<core::RecommendationList> params(new core::RecommendationList);
    params->minimumMatch = 1;
    params->pickRatio = 9999;
    params->alwaysPickAt = 1;
    params->name = "#similarfics";
    QString storedList = ui->cbRecGroup->currentText();
    auto storedLists = env.interfaces.recs->GetAllRecommendationListNames(true);
    QVector<int> sourceFics;
    sourceFics.push_back(id);

    auto result = env.BuildRecommendations(params, sourceFics, false, false);
    Q_UNUSED(result);

    auto lists = env.interfaces.recs->GetAllRecommendationListNames(true);
    SilentCall(ui->cbRecGroup)->setModel(new QStringListModel(lists));
    ui->cbRecGroup->setCurrentText(params->name);
    ui->cbSortMode->setCurrentText("Rec Count");
    env.interfaces.recs->SetCurrentRecommendationList(env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText()));
    on_pbLoadDatabase_clicked();
}


void MainWindow::SetClientMode()
{
    ui->widget_4->hide();
    //ui->wdgAdminActions->hide();
    //    ui->chkHeartProfile->setChecked(false);
    //    ui->chkHeartProfile->setVisible(false);
    ui->tabWidget->removeTab(2);
    ui->tabWidget->removeTab(1);
    ui->chkLimitPageSize->setChecked(true);
    //ui->chkLimitPageSize->setEnabled(false);

}

void MainWindow::on_pbOpenRecommendations_clicked()
{
    TaskProgressGuard guard(this);
    env.filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_recommendations, true);
    if(env.filter.isValid)
    {
        auto startRecLoad = std::chrono::high_resolution_clock::now();
        LoadData();
        auto elapsed = std::chrono::high_resolution_clock::now() - startRecLoad;

        qDebug() << "Loaded recs in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        ui->edtResults->setUpdatesEnabled(true);
        ui->edtResults->setReadOnly(true);
        holder->SetData(env.fanfics);
    }
}

void MainWindow::OpenRecommendationList(QString listName)
{
    env.filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_whole_list,
                                           false,
                                           listName);
    if(env.filter.isValid)
    {
        env.filter.sortMode = core::StoryFilter::sm_reccount;
        auto startRecLoad = std::chrono::high_resolution_clock::now();
        LoadData();
        auto elapsed = std::chrono::high_resolution_clock::now() - startRecLoad;
        qDebug() << "Loaded recs in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        ui->edtResults->setUpdatesEnabled(true);
        ui->edtResults->setReadOnly(true);
        holder->SetData(env.fanfics);
    }
}

int MainWindow::BuildRecommendations(QSharedPointer<core::RecommendationList> params, bool clearAuthors)
{
    TaskProgressGuard guard(this);
    auto result = env.BuildRecommendations(params, env.GetSourceFicsFromFile("lists/source.txt"), clearAuthors);
    if(result == -1)
    {
        QMessageBox::warning(nullptr, "Attention!", "Could not create a list with such parameters\n"
                                                    "Try using more source fics, or loosen the restrictions");
    }

    FillRecTagCombobox();
    FillRecommenderListView();

    return result;
}


FilterErrors MainWindow::ValidateFilter()
{
    FilterErrors result;

    bool wordSearch = ui->chkWordsPlus->isChecked() && !ui->leContainsWords->text().isEmpty();
    wordSearch = wordSearch || (ui->chkWordsMinus->isChecked() && !ui->leNotContainsWords->text().isEmpty());
    QString minWordCount= ui->cbMinWordCount->currentText();
    if(wordSearch && (ui->cbNormals->currentText().trimmed().isEmpty() && minWordCount.toInt() < 60000))
    {
        result.AddError("Word search is currently only possible if:");
        result.AddError("    1) A fandom is selected.");
        result.AddError("    2) Fic size is >60k words.");
        result.AddError("Generic word searches will only be possible after DB is upgraded");
    }
    bool listNotSelected = ui->cbRecGroup->currentText().trimmed().isEmpty();
    if(listNotSelected && ui->cbSortMode->currentText() == "Rec Count")
    {
        result.AddError("Sorting on recommendation count only makes sense, ");
        result.AddError("if a recommendation list is selected");
        result.AddError("");
        result.AddError("Please select a recommendation list to the right of Rec List: or create one");
    }
    auto currentListId = env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText());
    core::RecPtr list = env.interfaces.recs->GetList(currentListId);
    bool emptyList = !list || list->ficCount == 0;
    if(emptyList && (ui->cbSortMode->currentText() == "Rec Count"
                     || ui->chkSearchWithinList->isChecked()
                     || ui->sbMinimumListMatches->value() > 0))
    {
        result.AddError("Current filter doesn't make sense because selected recommendation list is empty");
    }
    return result;
}

core::StoryFilter MainWindow::ProcessGUIIntoStoryFilter(core::StoryFilter::EFilterMode mode,
                                                        bool useAuthorLink,
                                                        QString listToUse,
                                                        bool performFilterValidation)
{

    auto valueIfChecked = [](QCheckBox* box, auto value){
        if(box->isChecked())
            return value;
        return decltype(value)();
    };
    core::StoryFilter filter;
    auto filterState = ValidateFilter();
    if(performFilterValidation && filterState.hasErrors)
    {
        filter.isValid = false;
        QMessageBox::warning(nullptr, "Attention!",filterState.errors.join("\n"));
        return filter;
    }
    auto tags = ui->wdgTagsPlaceholder->GetSelectedTags();
    if(ui->chkEnableTagsFilter->isChecked())
        filter.activeTags = tags;
    filter.showRecSources = ui->chkShowSources->isChecked();
    qDebug() << "Active tags: " << filter.activeTags;
    filter.allowNoGenre = ui->chkNoGenre->isChecked();
    filter.allowUnfinished = ui->chkShowUnfinished->isChecked();
    filter.ensureActive = ui->chkActive->isChecked();
    filter.ensureCompleted= ui->chkComplete->isChecked();
    filter.fandom = GetCurrentFandomID();
    filter.otherFandomsMode = ui->chkOtherFandoms->isChecked();

    filter.genreExclusion = valueIfChecked(ui->chkGenreMinus, core::StoryFilter::ProcessDelimited(ui->leNotContainsGenre->text(), "###"));
    filter.genreInclusion = valueIfChecked(ui->chkGenrePlus,core::StoryFilter::ProcessDelimited(ui->leContainsGenre->text(), "###"));
    filter.wordExclusion = valueIfChecked(ui->chkWordsMinus, core::StoryFilter::ProcessDelimited(ui->leNotContainsWords->text(), "###"));
    filter.wordInclusion = valueIfChecked(ui->chkWordsPlus, core::StoryFilter::ProcessDelimited(ui->leContainsWords->text(), "###"));
    filter.ignoreAlreadyTagged = ui->chkIgnoreTags->isChecked();
    filter.crossoversOnly= ui->chkCrossovers->isChecked();
    filter.includeCrossovers = !ui->chkNonCrossovers->isChecked();
    //chkNonCrossovers
    filter.ignoreFandoms= ui->chkIgnoreFandoms->isChecked();
//    /filter.includeCrossovers =false; //ui->rbCrossovers->isChecked();
    filter.tagsAreUsedForAuthors = ui->wdgTagsPlaceholder->UseTagsForAuthors() && ui->wdgTagsPlaceholder->GetSelectedTags().size() > 0;
    filter.useRealGenres = ui->chkGenreUseImplied->isChecked();
    filter.genrePresenceForInclude = static_cast<core::StoryFilter::EGenrePresence>(ui->cbGenrePresenceTypeInclude->currentIndex());
    filter.rating = static_cast<core::StoryFilter::ERatingFilter>(ui->cbFicRating->currentIndex());
    if(!ui->leAuthorID->text().isEmpty())
        filter.useThisAuthor = ui->leAuthorID->text().toInt();

    if(ui->cbGenrePresenceTypeExclude->currentText() == "Medium")
        filter.genrePresenceForExclude = core::StoryFilter::gp_medium;
    else if(ui->cbGenrePresenceTypeExclude->currentText() == "Minimal")
        filter.genrePresenceForExclude = core::StoryFilter::gp_minimal;
    else if(ui->cbGenrePresenceTypeExclude->currentText() == "None")
        filter.genrePresenceForExclude = core::StoryFilter::gp_none;


    SlashFilterState slashState{
        ui->chkEnableSlashFilter->isChecked(),
                ui->chkApplyLocalSlashFilter->isChecked(),
                ui->chkInvertedSlashFilter->isChecked(),
                ui->chkOnlySlash->isChecked(),
                ui->chkInvertedSlashFilterLocal->isChecked(),
                ui->chkOnlySlashLocal->isChecked(),
                ui->chkEnableSlashExceptions->isChecked(),
                QList<int>{}, // temporary placeholder
        ui->cbSlashFilterAggressiveness->currentIndex(),
                ui->chkShowExactFilterLevel->isChecked(),
                ui->chkStrongMOnly->isChecked()
    };

    filter.slashFilter = slashState;
    filter.maxFics = valueIfChecked(ui->chkRandomizeSelection, ui->sbMaxRandomFicCount->value());
    filter.minFavourites = valueIfChecked(ui->chkFaveLimitActivated, ui->sbMinimumFavourites->value());
    filter.maxWords= ui->cbMaxWordCount->currentText().toInt();
    filter.minWords= ui->cbMinWordCount->currentText().toInt();
    filter.randomizeResults = ui->chkRandomizeSelection->isChecked();
    filter.recentAndPopularFavRatio = ui->sbFavrateValue->value();
    filter.recentCutoff = ui->dteFavRateCut->dateTime();
    filter.reviewBias = static_cast<core::StoryFilter::EReviewBiasMode>(ui->cbBiasFavor->currentIndex());
    filter.biasOperator = static_cast<core::StoryFilter::EBiasOperator>(ui->cbBiasOperator->currentIndex());
    filter.reviewBiasRatio = ui->leBiasValue->text().toDouble();
    filter.sortMode = static_cast<core::StoryFilter::ESortMode>(ui->cbSortMode->currentIndex() + 1);
    filter.minRecommendations =  ui->sbMinimumListMatches->value();
    filter.recordLimit = ui->chkLimitPageSize->isChecked() ?  ui->sbPageSize->value() : 5000;
    filter.recordPage = ui->chkLimitPageSize->isChecked() ?  0 : -1;
    filter.listOpenMode = ui->chkSearchWithinList->isChecked();
    //if(ui->cbSortMode->currentText())
    if(listToUse.isEmpty())
        filter.listForRecommendations = env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText());
    else
        filter.listForRecommendations = env.interfaces.recs->GetListIdForName(listToUse);
    //filter.titleInclusion = nothing for now
    filter.website = "ffn"; // just ffn for now
    filter.mode = mode;
    //filter.Log();
    return filter;
}


void MainWindow::on_pbReprocessAuthors_clicked()
{
    TaskProgressGuard guard(this);
    env.ProcessListIntoRecommendations("lists/source.txt");
}

void MainWindow::OnCopyFavUrls()
{
    TaskProgressGuard guard(this);
    QClipboard *clipboard = QApplication::clipboard();
    QString result;
    for(int i = 0; i < recommendersModel->rowCount(); i ++)
    {
        auto author = env.interfaces.authors->GetAuthorByNameAndWebsite(recommendersModel->index(i, 0).data().toString(), "ffn");
        if(!author)
            continue;
        result += author->url("ffn") + "\n";
    }
    clipboard->setText(result);
}




void MainWindow::on_chkRandomizeSelection_toggled(bool checked)
{
    //ui->chkRandomizeSelection->setEnabled(checked);
    ui->sbMaxRandomFicCount->setEnabled(checked);
}

void MainWindow::on_pbReinitFandoms_clicked()
{
    QString diagnostics;
    diagnostics+= "This operation will now reload fandom index pages.\n";
    diagnostics+= "It is only necessary if you need to add a new fandom.\n";
    diagnostics+= "Do you want to continue?\n";

    QMessageBox m;
    m.setIcon(QMessageBox::Warning);
    m.setText(diagnostics);
    auto yesButton =  m.addButton("Yes", QMessageBox::AcceptRole);
    auto noButton =  m.addButton("Cancel", QMessageBox::AcceptRole);
    Q_UNUSED(noButton);
    m.exec();
    if(m.clickedButton() != yesButton)
        return;

    UpdateFandomTask task;
    task.ffn = true;
    UpdateFandomList(task);
    env.interfaces.fandoms->Clear();
}

void MainWindow::OnOpenLogUrl(const QUrl & url)
{
    QDesktopServices::openUrl(url);
}

void MainWindow::OnWipeCache()
{
    An<PageManager> pager;
    pager->WipeAllCache();
}


void MainWindow::UpdateFandomList(UpdateFandomTask )
{
    TaskProgressGuard guard(this);

    ui->edtResults->clear();

    QSqlDatabase db = QSqlDatabase::database();
    FandomListReloadProcessor proc(db, env.interfaces.fanfics, env.interfaces.fandoms, env.interfaces.pageTask, env.interfaces.db);
    connect(&proc, &FandomListReloadProcessor::displayWarning, this, &MainWindow::OnWarningRequested);
    connect(&proc, &FandomListReloadProcessor::requestProgressbar, this, &MainWindow::OnProgressBarRequested);
    connect(&proc, &FandomListReloadProcessor::updateCounter, this, &MainWindow::OnUpdatedProgressValue);
    connect(&proc, &FandomListReloadProcessor::updateInfo, this, &MainWindow::OnNewProgressString);
    proc.UpdateFandomList();
}



void MainWindow::OnRemoveFandomFromRecentList()
{
    auto fandom = ui->lvTrackedFandoms->currentIndex().data(0).toString();
    env.interfaces.fandoms->RemoveFandomFromRecentList(fandom);
    env.interfaces.fandoms->ReloadRecentFandoms();
    recentFandomsModel->setStringList(env.interfaces.fandoms->GetRecentFandoms());
}

void MainWindow::OnRemoveFandomFromIgnoredList()
{
    auto fandom = ui->lvIgnoredFandoms->currentIndex().data(0).toString();
    env.interfaces.fandoms->RemoveFandomFromIgnoredList(fandom);
    ignoredFandomsModel->setStringList(env.interfaces.fandoms->GetIgnoredFandoms());
}

void MainWindow::OnRemoveFandomFromSlashFilterIgnoredList()
{
    auto fandom = ui->lvExcludedFandomsSlashFilter->currentIndex().data(0).toString();
    env.interfaces.fandoms->RemoveFandomFromIgnoredListSlashFilter(fandom);
    ignoredFandomsSlashFilterModel->setStringList(env.interfaces.fandoms->GetIgnoredFandomsSlashFilter());
}

void MainWindow::OnFandomsContextMenu(const QPoint &pos)
{
    fandomMenu.popup(ui->lvTrackedFandoms->mapToGlobal(pos));
}

void MainWindow::OnIgnoredFandomsContextMenu(const QPoint &pos)
{
    ignoreFandomMenu.popup(ui->lvIgnoredFandoms->mapToGlobal(pos));
}

void MainWindow::OnIgnoredFandomsSlashFilterContextMenu(const QPoint &pos)
{
    ignoreFandomSlashFilterMenu.popup(ui->lvExcludedFandomsSlashFilter->mapToGlobal(pos));
}

void MainWindow::on_pbFormattedList_clicked()
{
    if(ui->chkGroupFandoms->isChecked())
        OnDoFormattedListByFandoms();
    else
        OnDoFormattedList();
}

void MainWindow::on_pbCreateHTML_clicked()
{
    on_pbFormattedList_clicked();
    QString partialfileName = ui->cbRecGroup->currentText() + "_page_" + QString::number(env.pageOfCurrentQuery) + ".html";
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                                                    "FicLists/" + partialfileName,
                                                    tr("*.html"));
    if(fileName.isEmpty())
        return;

    QFile f(fileName);
    f.open( QIODevice::WriteOnly );
    QTextStream ts(&f);
    QClipboard *clipboard = QApplication::clipboard();
    QString header = "<!DOCTYPE html>"
                     "<html>"
                     "<head>"
                     "<title>Page Title</title>"
                     "</head>"
                     "<body>";
    QString footer = "</body>"
                     "</html>";
    ts << header;
    ts << clipboard->text();
    ts << footer;
    f.close();
}


void MainWindow::OnFindSimilarClicked(QVariant url)
{
    //auto id = url_utils::GetWebId(url.toString(), "ffn");
    //auto id = env.interfaces.fanfics->GetIDFromWebID(url.toInt(),"ffn");
    if(url == "-1")
        return;
    ResetFilterUItoDefaults();
    ui->cbNormals->setCurrentText("");
    ui->leContainsGenre->setText("");
    ui->leNotContainsGenre->setText("");
    ui->leContainsWords->setText("");
    ui->leNotContainsWords->setText("");
    CreateSimilarListForGivenFic(url.toInt());
}

void MainWindow::on_pbIgnoreFandom_clicked()
{
    env.interfaces.fandoms->IgnoreFandom(ui->cbIgnoreFandomSelector->currentText(), ui->chkIgnoreIncludesCrossovers->isChecked());
    ignoredFandomsModel->setStringList(env.interfaces.fandoms->GetIgnoredFandoms());
}

void MainWindow::OnUpdatedProgressValue(int value)
{
    pbMain->setValue(value);
}

void MainWindow::OnNewProgressString(QString value)
{
    AddToProgressLog(value);
}

void MainWindow::OnResetTextEditor()
{
    ui->edtResults->clear();
}

void MainWindow::OnProgressBarRequested(int value)
{
    pbMain->setMaximum(value);
    pbMain->show();
}

void MainWindow::OnWarningRequested(QString value)
{
    QMessageBox::information(nullptr, "Attention!", value);
}

void MainWindow::OnFillDBIdsForTags()
{
    env.FillDBIDsForTags();
}

void MainWindow::OnTagReloadRequested()
{
    ProcessTagsIntoGui();
    tagList = env.interfaces.tags->ReadUserTags();
    qwFics->rootContext()->setContextProperty("tagModel", tagList);
}


void MainWindow::on_chkRecsAutomaticSettings_toggled(bool checked)
{
    if(checked)
    {
        ui->leRecsAlwaysPickAt->setEnabled(false);
        ui->leRecsPickRatio->setEnabled(false);
        ui->leRecsMinimumMatches->setEnabled(false);
        LoadAutomaticSettingsForRecListSources(ui->edtRecsContents->toPlainText().split("\n").size());
    }
    else
    {
        ui->leRecsAlwaysPickAt->setEnabled(true);
        ui->leRecsPickRatio->setEnabled(true);
        ui->leRecsMinimumMatches->setEnabled(true);
    }
}

void MainWindow::on_pbRecsLoadFFNProfileIntoSource_clicked()
{
    LoadFFNProfileIntoTextBrowser(ui->edtRecsContents, ui->leRecsFFNUrl);
}

void MainWindow::on_pbRecsCreateListFromSources_clicked()
{
    if(ui->chkAutomaticLike->isChecked())
    {
        QMessageBox m;
        m.setIcon(QMessageBox::Warning);
        m.setText("\"Automatic Like\" option is enabled!\n"
                  "Only enable this while you are loading your own favourite list.\n"
                  "Otherwise you will not see fics from that URL in your searches.\n"
                  "Are you sure you want to continue?");
        auto yesButton =  m.addButton("Yes", QMessageBox::AcceptRole);
        auto noButton =  m.addButton("No", QMessageBox::AcceptRole);
        Q_UNUSED(noButton);
        m.exec();
        if(m.clickedButton() != yesButton)
            return;
    }

    QSharedPointer<core::RecommendationList> params(new core::RecommendationList);
    params->name = ui->leRecsListName->text();
    if(params->name.trimmed().isEmpty())
    {
        QMessageBox::warning(nullptr, "Warning!", "Please name your list.");
        return;
    }
    params->minimumMatch = ui->leRecsMinimumMatches->text().toInt();
    params->pickRatio = ui->leRecsPickRatio->text().toInt();
    params->alwaysPickAt = ui->leRecsAlwaysPickAt->text().toInt();
    params->useWeighting = ui->cbRecsAlgo->currentText() == "Weighted";
    TaskProgressGuard guard(this);

    QVector<int> sourceFics = PickFicIDsFromTextBrowser(ui->edtRecsContents);
    auto result = env.BuildRecommendations(params, sourceFics, ui->chkAutomaticLike->isChecked(), false);
    if(result == -1)
    {
        QMessageBox::warning(nullptr, "Attention!", "Could not create a list with such parameters\n"
                                                    "Try using more source fics, or loosen the restrictions");
        return;
    }
    Q_UNUSED(result);
    auto lists = env.interfaces.recs->GetAllRecommendationListNames(true);
    SilentCall(ui->cbRecGroup)->setModel(new QStringListModel(lists));
    ui->cbRecGroup->setCurrentText(params->name);
    ui->cbSortMode->setCurrentText("Rec Count");
    env.interfaces.recs->SetCurrentRecommendationList(env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText()));
    ui->pbGetSourceLinks->setEnabled(true);
    ui->pbDeleteRecList->setEnabled(true);
    on_pbLoadDatabase_clicked();
}





void MainWindow::on_pbReapplyFilteringMode_clicked()
{
    on_cbCurrentFilteringMode_currentTextChanged(ui->cbCurrentFilteringMode->currentText());
}

void MainWindow::ResetFilterUItoDefaults()
{
    ui->chkRandomizeSelection->setChecked(false);
    ui->chkEnableSlashFilter->setChecked(true);
    ui->chkEnableTagsFilter->setChecked(false);
    ui->chkIgnoreFandoms->setChecked(true);
    ui->chkComplete->setChecked(false);
    ui->chkShowUnfinished->setChecked(true);
    ui->chkActive->setChecked(false);
    ui->chkNoGenre->setChecked(true);
    ui->chkIgnoreTags->setChecked(false);
    ui->chkOtherFandoms->setChecked(false);
    ui->chkGenrePlus->setChecked(false);
    ui->chkGenreMinus->setChecked(false);
    ui->chkWordsPlus->setChecked(false);
    ui->chkWordsMinus->setChecked(false);
    ui->chkFaveLimitActivated->setChecked(false);
    ui->chkLimitPageSize->setChecked(true);
    //ui->chkHeartProfile->setChecked(false);
    ui->chkInvertedSlashFilter->setChecked(true);
    ui->chkInvertedSlashFilterLocal->setChecked(true);
    ui->chkOnlySlash->setChecked(false);
    ui->chkOnlySlashLocal->setChecked(false);
    ui->chkApplyLocalSlashFilter->setChecked(true);
    ui->chkCrossovers->setChecked(false);
    ui->chkNonCrossovers->setChecked(false);

    ui->cbMinWordCount->setCurrentText("0");
    ui->cbMaxWordCount->setCurrentText("");
    ui->cbBiasFavor->setCurrentText("None");
    ui->cbBiasOperator->setCurrentText(">");
    ui->cbSlashFilterAggressiveness->setCurrentText("Medium");
    ui->cbSortMode->setCurrentIndex(0);

    ui->leBiasValue->setText("11.");
    QDateTime dt = QDateTime::currentDateTimeUtc().addYears(-1);
    ui->dteFavRateCut->setDateTime(dt);
    ui->sbFavrateValue->setValue(4);
    ui->sbPageSize->setValue(100);
    ui->sbMaxRandomFicCount->setValue(6);
    ui->chkSearchWithinList->setChecked(false);
    ui->chkShowSources->setChecked(false);
    ui->chkGenreUseImplied->setChecked(false);
    ui->sbMinimumListMatches->setValue(0);
    ui->wdgTagsPlaceholder->ClearSelection();

}

void MainWindow::DetectGenreSearchState()
{
    if(ui->chkGenreUseImplied->isChecked())
    {
        ui->lblGenreInclude->setEnabled(true);
        ui->lblGenreExclude->setEnabled(true);
        ui->cbGenrePresenceTypeExclude->setEnabled(true);
        ui->cbGenrePresenceTypeInclude->setEnabled(true);
    }
    else
    {
        ui->lblGenreInclude->setEnabled(false);
        ui->lblGenreExclude->setEnabled(false);
        ui->cbGenrePresenceTypeExclude->setEnabled(false);
        ui->cbGenrePresenceTypeInclude->setEnabled(false);
    }
}

void MainWindow::DetectSlashSearchState()
{
    if(ui->cbSlashFilterAggressiveness->currentText() == "Strong")
    {
        ui->chkStrongMOnly->setEnabled(true);
    }
    else
    {
        ui->chkStrongMOnly->setEnabled(false);
        ui->chkStrongMOnly->setChecked(false);
    }
}

void MainWindow::LoadFFNProfileIntoTextBrowser(QTextBrowser*edit, QLineEdit* leUrl)
{
    auto fics = LoadFavourteLinksFromFFNProfile(leUrl->text());
    if(fics.size() == 0)
        return;

    LoadAutomaticSettingsForRecListSources(fics.size());
    edit->setOpenExternalLinks(false);
    edit->setOpenLinks(false);
    edit->setReadOnly(false);
    auto font = edit->font();
    font.setPixelSize(14);

    edit->setFont(font);
    std::sort(fics.begin(), fics.end(), [](QSharedPointer<core::Fic> f1, QSharedPointer<core::Fic> f2){
        return f1->author->name < f2->author->name;
    });

    for(auto fic: fics)
    {
        QString url = url_utils::GetStoryUrlFromWebId(fic->ffn_id, "ffn");
        QString toInsert = "<a href=\"" + url + "\"> %1 </a>";
        edit->insertHtml(fic->author->name + "<br>" +  fic->title + "<br>" + toInsert.arg(url) + "<br>");
        edit->insertHtml("<br>");
    }
}

QVector<int> MainWindow::PickFicIDsFromTextBrowser(QTextBrowser * edit)
{
    return PickFicIDsFromString(edit->toPlainText());
}

QVector<int> MainWindow::PickFicIDsFromString(QString str)
{
    QVector<int> sourceFics;
    QStringList lines = str.split("\n");
    QRegularExpression rx("https://www.fanfiction.net/s/(\\d+)");
    for(auto line : lines)
    {
        if(!line.startsWith("http"))
            continue;
        auto match = rx.match(line);
        if(!match.hasMatch())
            continue;
        QString val = match.captured(1);
        sourceFics.push_back(val.toInt());
    }
    return sourceFics;
}

void MainWindow::AnalyzeIdList(QVector<int> ficIDs)
{
    ui->edtAnalysisResults->clear();
    ui->edtAnalysisSources->setReadOnly(false);
    auto stats = env.GetStatsForFicList(ficIDs);
    if(!stats.isValid)
    {
        //QMessageBox::warning(nullptr, "Warning!", "Could not analyze the list.");
        return;
    }

    ui->edtAnalysisResults->insertHtml("Analysis complete.<br>");
    ui->edtAnalysisResults->insertHtml(QString("Fics in the list: <font color=blue>%1</font><br>").arg(QString::number(stats.favourites)));
    if(stats.noInfoCount > 0)
        ui->edtAnalysisResults->insertHtml(QString("No info in DB for: <font color=blue>%1</font><br>").arg(QString::number(stats.noInfoCount)));
    ui->edtAnalysisResults->insertHtml(QString("Total count of words: <font color=blue>%1</font><br>").arg(QString::number(stats.ficWordCount)));
    ui->edtAnalysisResults->insertHtml(QString("Average words per chapter: <font color=blue>%1</font><br>").arg(QString::number(stats.averageWordsPerChapter)));
    ui->edtAnalysisResults->insertHtml(QString("Average words per fic: <font color=blue>%1</font><br>").arg(QString::number(stats.averageLength)));
    ui->edtAnalysisResults->insertHtml("<br>");
    ui->edtAnalysisResults->insertHtml(QString("First published fic: <font color=blue>%1</font><br>")
                                       .arg(stats.firstPublished.toString("yyyy-MM-dd")));
    ui->edtAnalysisResults->insertHtml(QString("Last published fic: <font color=blue>%1</font><br>")
                                       .arg(stats.lastPublished.toString("yyyy-MM-dd")));
    ui->edtAnalysisResults->insertHtml("<br>");

    ui->edtAnalysisResults->insertHtml(QString("Slash content%: <font color=blue>%1</font><br>").arg(QString::number(stats.slashRatio*100)));
    //ui->edtAnalysisResults->insertHtml(QString("Mature content%: <font color=blue>%1</font><br>").arg(QString::number(stats.esrbMature*100)));

    ui->edtAnalysisResults->insertHtml("<br>");


    QVector<SortedBit<double>> sizes = {{stats.sizeFactors[0], "Small"},
                                        {stats.sizeFactors[1], "Medium"},
                                        {stats.sizeFactors[2], "Large"},
                                        {stats.sizeFactors[3], "Huge"}};
    ui->edtAnalysisResults->insertHtml("Sizes%:<br>");
    QString sizeTemplate = "%1: <font color=blue>%2</font>";
    std::sort(std::begin(sizes), std::end(sizes), [](const SortedBit<double>& m1,const SortedBit<double>& m2){
        return m1.value > m2.value;
    });
    for(auto size : sizes)
    {
        QString tmp = sizeTemplate;
        tmp=tmp.arg(size.name).arg(QString::number(size.value*100));
        ui->edtAnalysisResults->insertHtml(tmp + "<br>");
    }
    ui->edtAnalysisResults->insertHtml("<br>");



    ui->edtAnalysisResults->insertHtml(QString("Mood uniformity: <font color=blue>%1</font><br>").arg(QString::number(stats.moodUniformity)));
    ui->edtAnalysisResults->insertHtml("Mood content%:<br>");

    QString moodTemplate = "%1: <font color=blue>%2</font>";

    QVector<SortedBit<double>> moodsVec = {{stats.moodSad,  "sad"},{stats.moodNeutral, "neutral"},{stats.moodHappy, "happy"}};
    std::sort(std::begin(moodsVec), std::end(moodsVec), [](const SortedBit<double>& m1,const SortedBit<double>& m2){
        return m1.value > m2.value;
    });
    for(auto mood : moodsVec)
    {
        QString tmp = moodTemplate;
        tmp=tmp.arg(mood.name.rightJustified(8)).arg(QString::number(mood.value*100));
        ui->edtAnalysisResults->insertHtml(tmp + "<br>");
    }
    ui->edtAnalysisResults->insertHtml("<br>");

    QVector<SortedBit<double>> genres;
    for(auto genreKey : stats.genreFactors.keys())
        genres.push_back({stats.genreFactors[genreKey],genreKey});
    std::sort(std::begin(genres), std::end(genres), [](const SortedBit<double>& g1,const SortedBit<double>& g2){
        return g1.value > g2.value;
    });
    QString genreTemplate = "%1: <font color=blue>%2</font>";


    ui->edtAnalysisResults->insertHtml(QString("Genre diversity: <font color=blue>%1</font><br>").arg(QString::number(stats.genreDiversityFactor)));
    ui->edtAnalysisResults->insertHtml("Genres%:<br>");
    for(int i = 0; i < 10; i++)
    {
        QString tmp = genreTemplate;
        tmp=tmp.arg(genres[i].name).arg(QString::number(genres[i].value*100));
        ui->edtAnalysisResults->insertHtml(tmp + "<br>");
    }
    ui->edtAnalysisResults->insertHtml("<br>");



    ui->edtAnalysisResults->insertHtml(QString("Fandom diversity: <font color=blue>%1</font><br>").arg(QString::number(stats.fandomsDiversity)));
    ui->edtAnalysisResults->insertHtml("Fandoms:<br>");
    QVector<SortedBit<int>> fandoms;
    for(auto id : stats.fandomsConverted.keys())
    {
        QString name = env.interfaces.fandoms->GetNameForID(id);
        int count = stats.fandomsConverted[id];
        fandoms.push_back({count, name});
    }
    std::sort(std::begin(fandoms), std::end(fandoms), [](const SortedBit<int>& g1,const SortedBit<int>& g2){
        return g1.value > g2.value;
    });
    QString fandomTemplate = "%1: <font color=blue>%2</font>";
    for(int i = 0; i < 20; i++)
    {
        QString tmp = fandomTemplate;
        if(i >= fandoms.size())
            break;
        tmp=tmp.arg(fandoms[i].name).arg(QString::number(fandoms[i].value));
        ui->edtAnalysisResults->insertHtml(tmp + "<br>");
    }
}

void MainWindow::AnalyzeCurrentFilter()
{
    QString result;
    for(int i = 0; i < typetableModel->rowCount(); i ++)
    {
        if(ui->chkInfoForLinks->isChecked())
        {
            result += typetableModel->index(i, 2).data().toString() + "\n";
        }
        result += "https://www.fanfiction.net/s/" + typetableModel->index(i, 9).data().toString() + "\n";//\n
    }
    auto ids = PickFicIDsFromString(result);
    AnalyzeIdList(ids);
}

void MainWindow::on_cbCurrentFilteringMode_currentTextChanged(const QString &)
{
    if(ui->cbCurrentFilteringMode->currentText() == "Default")
    {
        ResetFilterUItoDefaults();
    }

    if(ui->cbCurrentFilteringMode->currentText() == "Random Recs")
    {
        ResetFilterUItoDefaults();
        ui->chkRandomizeSelection->setChecked(true);
        ui->sbMaxRandomFicCount->setValue(6);
        ui->cbSortMode->setCurrentText("Rec Count");
        ui->chkSearchWithinList->setChecked(true);
        ui->sbMinimumListMatches->setValue(1);
    }
    if(ui->cbCurrentFilteringMode->currentText() == "Tag Search")
    {
        ResetFilterUItoDefaults();
        ui->chkEnableTagsFilter->setChecked(true);
    }
}

void MainWindow::on_cbRecGroup_currentTextChanged(const QString &)
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    settings.setValue("Settings/currentList", ui->cbRecGroup->currentText());
    settings.sync();
    env.interfaces.recs->SetCurrentRecommendationList(env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText()));
}

void MainWindow::on_pbUseProfile_clicked()
{
    ui->edtRecsContents->clear();
    LoadFFNProfileIntoTextBrowser(ui->edtRecsContents, ui->leRecsFFNUrl);
    on_pbRecsLoadFFNProfileIntoSource_clicked();
    if(ui->edtRecsContents->toPlainText().trimmed().size() == 0)
        return;
    ResetFilterUItoDefaults();
    on_pbRecsCreateListFromSources_clicked();
}

void MainWindow::on_pbMore_clicked()
{
    if(!ui->wdgDetailedRecControl->isVisible())
    {
        ui->spRecsFan->setCollapsible(0,0);
        ui->spRecsFan->setCollapsible(1,0);
        ui->wdgDetailedRecControl->show();
        ui->spRecsFan->setSizes({1000,0});
        ui->pbMore->setText("Less");
    }
    else
    {
        ui->spRecsFan->setCollapsible(0,0);
        ui->spRecsFan->setCollapsible(1,0);
        ui->wdgDetailedRecControl->hide();
        ui->spRecsFan->setSizes({0,1000});
        ui->pbMore->setText("More");
    }
}


void MainWindow::on_sbMinimumListMatches_valueChanged(int value)
{
    if(value > 0)
        ui->chkSearchWithinList->setChecked(true);
}

void MainWindow::on_chkOtherFandoms_toggled(bool checked)
{
    if(checked)
        ui->chkIgnoreFandoms->setChecked(false);
}

void MainWindow::on_chkIgnoreFandoms_toggled(bool checked)
{
    if(checked)
        ui->chkOtherFandoms->setChecked(false);
}

void MainWindow::on_pbDeleteRecList_clicked()
{
    QString diagnostics;
    diagnostics+= "This will delete currently active recommendation list.\n";
    diagnostics+= "Do you want to delete it?";
    QMessageBox m; /*(QMessageBox::Warning, "Unfinished task warning",
                  diagnostics,QMessageBox::Ok|QMessageBox::Cancel);*/
    m.setIcon(QMessageBox::Warning);
    m.setText(diagnostics);
    auto deleteTask =  m.addButton("Yes",QMessageBox::AcceptRole);
    auto noTask =      m.addButton("No",QMessageBox::AcceptRole);
    Q_UNUSED(noTask);
    m.exec();
    if(m.clickedButton() == deleteTask)
    {
        auto id = env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText());
        env.interfaces.recs->DeleteList(id);
        FillRecTagCombobox();
    }
    else
    {
        //do nothing
    }

}

void MainWindow::on_pbGetSourceLinks_clicked()
{
    auto listId = env.interfaces.recs->GetListIdForName(ui->cbRecGroup->currentText());
    auto sources = env.GetListSourceFFNIds(listId);
    QString result;
    for(auto source: sources)
        result+="https://www.fanfiction.net/s/" + QString::number(source)  + "\n";
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(result);
}

void MainWindow::on_pbProfileCompare_clicked()
{
    ui->edtAnalysisResults->clear();
    ui->edtAnalysisResults->setOpenExternalLinks(false);
    ui->edtAnalysisResults->setOpenLinks(false);
    ui->edtAnalysisResults->setReadOnly(false);
    auto font = ui->edtAnalysisResults->font();
    font.setPixelSize(14);

    ui->edtAnalysisResults->setFont(font);

    QList<QSharedPointer<core::Fic> > result;

    auto ficsLeft = LoadFavourteLinksFromFFNProfile(ui->leFFNProfileLeft->text());
    auto ficsRight = LoadFavourteLinksFromFFNProfile(ui->leFFNProfileRight->text());
    if(ficsLeft.size() == 0 || ficsRight.size() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", "One of the lists is empty or ould not be acquired");
        return;
    }

    std::sort(ficsLeft.begin(), ficsLeft.end(), [](QSharedPointer<core::Fic> f1, QSharedPointer<core::Fic> f2){
        return f1->ffn_id < f2->ffn_id;
    });

    std::sort(ficsRight.begin(), ficsRight.end(), [](QSharedPointer<core::Fic> f1, QSharedPointer<core::Fic> f2){
        return f1->ffn_id < f2->ffn_id;
    });

    std::set_intersection(ficsLeft.begin(), ficsLeft.end(),
                          ficsRight.begin(), ficsRight.end(),
                          std::back_inserter(result), [](QSharedPointer<core::Fic> f1, QSharedPointer<core::Fic> f2){
        return f1->ffn_id < f2->ffn_id;
    });

    std::sort(result.begin(), result.end(), [](QSharedPointer<core::Fic> f1, QSharedPointer<core::Fic> f2){
        return f1->author->name < f2->author->name;
    });

    ui->edtAnalysisResults->insertHtml(QString("First user has %1 favourites.").arg(QString::number(ficsLeft.size())));
    ui->edtAnalysisResults->insertHtml("<br>");
    ui->edtAnalysisResults->insertHtml(QString("Second user has %1 favourites.").arg(QString::number(ficsRight.size())));
    ui->edtAnalysisResults->insertHtml("<br>");
    ui->edtAnalysisResults->insertHtml(QString("They have %1 favourites in common.").arg(QString::number(result.size())));
    ui->edtAnalysisResults->insertHtml("<br>");
    for(auto fic: result)
    {
        QString url = url_utils::GetStoryUrlFromWebId(fic->ffn_id, "ffn");
        QString toInsert = "<a href=\"" + url + "\"> %1 </a>";
        ui->edtAnalysisResults->insertHtml(fic->author->name + "<br>" +  fic->title + "<br>" + toInsert.arg(url) + "<br>");
        ui->edtAnalysisResults->insertHtml("<br>");
    }
}

void MainWindow::on_chkGenreUseImplied_stateChanged(int )
{
    DetectGenreSearchState();
}

void MainWindow::on_cbSlashFilterAggressiveness_currentIndexChanged(int index)
{
    DetectSlashSearchState();
}

void MainWindow::on_pbLoadUrlForAnalysis_clicked()
{
    LoadFFNProfileIntoTextBrowser(ui->edtAnalysisSources, ui->leFFNProfileLoad);
}

void MainWindow::on_pbAnalyzeListOfFics_clicked()
{

    auto ficIDs = PickFicIDsFromTextBrowser(ui->edtAnalysisSources);
    if(ficIDs.size() == 0)
    {
        QMessageBox::warning(nullptr, "Warning!", "There are no FFN urls in the source section.\n"
                                                  "Either load your profile with the button to the right\n"
                                                  "or drop a bunch of FFN URLs below.");
        return;
    }
    AnalyzeIdList(ficIDs);

}
