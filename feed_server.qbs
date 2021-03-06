import qbs 1.0
import qbs.Process
import "BaseDefines.qbs" as App
import "Precompiled.qbs" as Precompiled


App{
    name: "feed_server"
    consoleApplication:false
    type:"application"
    qbsSearchPaths: [sourceDirectory + "/modules", sourceDirectory + "/repo_modules"]
    Depends { name: "Qt.core"}
    Depends { name: "Qt.sql" }
    Depends { name: "Qt.core" }
    Depends { name: "Qt.network" }
    Depends { name: "Qt.gui" }
    Depends { name: "Qt.concurrent" }
    Depends { name: "cpp" }
    Depends { name: "logger" }
    Depends { name: "proto_generation" }
    Depends { name: "grpc_generation" }

    Depends { name: "projecttype" }

    Precompiled{condition:conditionals.usePrecompiledHeader}
    cpp.minimumWindowsVersion: "6.0"

    cpp.defines: base.concat(["L_LOGGER_LIBRARY"])
    cpp.includePaths: [
        sourceDirectory,
        //sourceDirectory + "/../",
        sourceDirectory + "/include",
        sourceDirectory + "/libs",
        sourceDirectory + "/third_party/zlib",
        sourceDirectory + "/libs/Logger/include",
    ]

    files: [
        "include/calc_data_holder.h",
        "include/core/fav_list_analysis.h",
        "include/grpc/grpc_source.h",
        "include/Interfaces/data_source.h",
        "include/threaded_data/common_traits.h",
        "include/threaded_data/threaded_load.h",
        "include/threaded_data/threaded_save.h",
        "src/calc_data_holder.cpp",
        "src/core/fav_list_analysis.cpp",
        "src/grpc/grpc_source.cpp",
        "src/Interfaces/data_source.cpp",
        "include/Interfaces/base.h",
        "include/Interfaces/genres.h",
        "include/Interfaces/fandoms.h",
        "include/Interfaces/fanfics.h",
        "include/Interfaces/ffn/ffn_fanfics.h",
        "include/Interfaces/ffn/ffn_authors.h",
        "include/Interfaces/db_interface.h",
        "include/Interfaces/interface_sqlite.h",
        "include/Interfaces/recommendation_lists.h",
        "src/Interfaces/authors.cpp",
        "src/Interfaces/base.cpp",
        "src/Interfaces/genres.cpp",
        "src/Interfaces/fandoms.cpp",
        "src/Interfaces/db_interface.cpp",
        "src/Interfaces/ffn/ffn_authors.cpp",
        "src/Interfaces/interface_sqlite.cpp",
        "src/Interfaces/recommendation_lists.cpp",
        "include/container_utils.h",
        "include/generic_utils.h",
        "include/timeutils.h",
        "include/servers/feed.h",
        "src/generic_utils.cpp",
        "include/querybuilder.h",
        "include/queryinterfaces.h",
        "include/core/section.h",
        "include/storyfilter.h",
        "include/url_utils.h",
        "src/threaded_data/threaded_load.cpp",
        "src/threaded_data/threaded_save.cpp",
        "third_party/roaring/roaring.c",
        "third_party/roaring/roaring.h",
        "third_party/roaring/roaring.hh",
        "third_party/sqlite3/sqlite3.c",
        "third_party/sqlite3/sqlite3.h",
        "src/sqlcontext.cpp",
        "src/main_feed_server.cpp",
        "src/pure_sql.cpp",
        "src/querybuilder.cpp",
        "src/regex_utils.cpp",
        "src/core/section.cpp",
        "src/sqlitefunctions.cpp",
        "src/storyfilter.cpp",
        "src/url_utils.cpp",
        "src/rng.cpp",
        "include/rng.h",
        "src/feeder_environment.cpp",
        "include/feeder_environment.h",
        "src/transaction.cpp",
        "include/transaction.h",
        "src/pagetask.cpp",
        "include/pagetask.h",
        "include/favholder.h",
        "src/favholder.cpp",
        "include/tokenkeeper.h",
        "include/in_tag_accessor.h",
        "src/in_tag_accessor.cpp",
        "src/servers/feed.cpp",
        "src/Interfaces/fanfics.cpp",
        "src/Interfaces/ffn/ffn_fanfics.cpp",
        "src/servers/token_processing.cpp",
        "include/servers/token_processing.h",
        "src/servers/database_context.cpp",
        "include/servers/database_context.h",
    ]
    cpp.systemIncludePaths: [
        sourceDirectory +"/proto",
        sourceDirectory + "/third_party",
        sourceDirectory + "/../"]

    cpp.staticLibraries: {
        //var libs = ["UniversalModels", "logger", "quazip"]
        var libs = []
        if(qbs.toolchain.contains("msvc"))
            libs = ["logger"]
        else{
            libs = ["logger", "dl", "protobuf"]
        }
        libs = libs.concat(conditionals.zlib)
        libs = libs.concat(conditionals.ssl)

        if(qbs.toolchain.contains("msvc"))
            libs = libs.concat(["User32","Ws2_32", "gdi32", "Advapi32"])
        if(qbs.toolchain.contains("msvc"))
            libs = libs.concat([conditionals.protobufName,"grpc", "grpc++", "gpr"])
        else
            libs = libs.concat(["grpc", "grpc++", "gpr"])
        return libs
    }

    Group{
        name:"grpc files"
        proto_generation.rootDir: conditionals.projectPath + "/proto"
        grpc_generation.rootDir: conditionals.projectPath + "/proto"
        proto_generation.protobufDependencyDir: conditionals.projectPath + "../"
        grpc_generation.protobufDependencyDir: conditionals.projectPath + "../"
        proto_generation.toolchain : qbs.toolchain
        grpc_generation.toolchain : qbs.toolchain
        files: [
            "proto/feeder_service.proto",
        ]
        fileTags: ["grpc", "proto"]
    }
    Group{
        name:"proto files"
        proto_generation.rootDir: conditionals.projectPath + "/proto"
        proto_generation.protobufDependencyDir: conditionals.projectPath + "../"
        proto_generation.toolchain : qbs.toolchain
        files: [
            "proto/filter.proto",
            "proto/fanfic.proto",
            "proto/fandom.proto",
            "proto/favlist.proto",
        ]
        fileTags: ["proto"]
    }
}
