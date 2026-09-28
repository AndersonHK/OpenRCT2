// STAGED standalone frozen-core fixture producer. Link against the pinned
// original core; no Vulkan library, new renderer or live raw publisher needed.
#ifndef MIXED_FIXTURE_FROZEN_ORACLE
#error Mixed reference corpus must be built against the qualified frozen core
#endif
#include "MixedFixtureWorld.h"
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/localisation/Language.h>
#include <iostream>
#include <map>

int main(int argc,char** argv)
{
    using namespace OpenRCT2;
    try
    {
        std::map<std::string,std::string> args;
        for(int i=1;i<argc;i+=2)
        {
            MixedFixtureWorld::Require(i+1<argc && std::string_view(argv[i]).starts_with("--"),"Expected --name value pairs");
            MixedFixtureWorld::Require(args.emplace(argv[i]+2,argv[i+1]).second,"Duplicate argument");
        }
        for(const auto* name:{"park","data","rct2","profile","output","oracle-receipt-sha256"})
            MixedFixtureWorld::Require(args.contains(name),"Missing required frozen fixture argument");
        const auto& receipt=args.at("oracle-receipt-sha256");
        MixedFixtureWorld::Require(receipt.size()==64 && std::all_of(receipt.begin(),receipt.end(),[](char c){
            return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Oracle receipt must be a lowercase SHA-256");
        gCustomUserDataPath=args.at("profile");gCustomOpenRCT2DataPath=args.at("data");gCustomRCT2DataPath=args.at("rct2");
        gOpenRCT2Headless=true;gOpenRCT2NoGraphics=false;
        MixedFixtureWorld::Require(Config::SetDefaults(),"Configuration defaults failed");
        Config::Get().general.language=LANGUAGE_ENGLISH_UK;
        auto context=CreateContext();Config::Get().general.rct2Path=args.at("rct2");
        MixedFixtureWorld::Require(context->Initialise(),"Frozen fixture graphics initialization failed");
        const auto extension=std::filesystem::path(args.at("park")).extension().string();
        MixedFixtureWorld::Require(extension==".park" || extension==".sv6","Expected .park or .sv6 asset donor");
        auto importer=extension==".park" ? ParkImporter::CreateParkFile(context->GetObjectRepository())
                                        : ParkImporter::CreateS6(context->GetObjectRepository());
        const auto loaded=importer->LoadSavedGame(args.at("park"),false);
        context->GetObjectManager().LoadObjects(loaded.RequiredObjects);importer->Import(getGameState());
        MixedFixtureWorld::Capture(args.at("output"),receipt);
        std::cout<<"Captured finite mixed original-painter corpus in "<<args.at("output")<<'\n';return 0;
    }
    catch(const std::exception& e) {std::cerr<<"Frozen mixed fixture failed: "<<e.what()<<'\n';return 1;}
}
