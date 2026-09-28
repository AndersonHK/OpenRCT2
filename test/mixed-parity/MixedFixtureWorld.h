// STAGED frozen-core diagnostic producer. No renderer or new publication dependency.
#pragma once
#include "MixedFixtureCapture.h"
#include "MixedFixtureRecipes.h"
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/FilterPaletteIds.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/PeepAnimationsObject.h>
#include <openrct2/object/SmallSceneryObject.h>
#include <openrct2/object/TerrainSurfaceObject.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/peep/PeepSpriteIds.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/ted/TrackElemType.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/TileElementsView.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TrackElement.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>

namespace MixedFixtureWorld
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    namespace Mixed=OpenRCT2::Ui::Gpu::MixedFixture;
    inline void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
    // Test-owned fixed ABI, deliberately independent of current-core publisher.
    struct Raw
    {
        int32_t x,y,z; uint32_t id; int32_t px,py,pz; uint32_t generation;
        uint32_t object,generationObject,tick,previousTick,orientation,action,group,type,nextType,frame,colours,accessories;
        uint32_t width,heightMin,heightMax,flags;
    };
    struct Descriptor { uint32_t object,generation,groups,offset,base,count,r0,r1; };
    struct Fact { uint32_t base,valid,r0,r1; };
    static_assert(sizeof(Raw)==96 && sizeof(Descriptor)==32 && sizeof(Fact)==16);
    inline Raw Read(const Peep& p)
    {
        Raw raw{p.x,p.y,p.z,p.id.ToUnderlying(),p.x,p.y,p.z,1,p.animationObjectIndex,1,0,0,
            p.orientation,uint32_t(p.action),uint32_t(p.animationGroup),uint32_t(p.animationType),uint32_t(p.nextAnimationType),
            p.animationImageIdOffset,uint32_t(p.getTShirtColour())|(uint32_t(p.getTrousersColour())<<8),0,
            p.spriteData.width,p.spriteData.heightMin,p.spriteData.heightMax,1u|(uint32_t(p.state)<<8)};
        if(p.type==EntityType::staff) { raw.flags|=2; raw.accessories=uint32_t(static_cast<const Staff&>(p).assignedStaffType)<<24; }
        else { const auto& g=static_cast<const Guest&>(p); raw.accessories=uint32_t(g.getHatColour())|(uint32_t(g.getBalloonColour())<<8)|(uint32_t(g.getUmbrellaColour())<<16); }
        return raw;
    }
    inline void Pose(Peep& p,ObjectEntryIndex object,PeepAnimationGroup group,const CoordsXYZ& pos,uint8_t orientation)
    {
        auto* animations=GetContext()->GetObjectManager().GetLoadedObject<PeepAnimationsObject>(object);
        Require(animations && uint32_t(group)<animations->GetNumAnimationGroups(),"Missing fixture peep animation group");
        p.animationObjectIndex=object; p.animationGroup=group; p.animationType=p.nextAnimationType=PeepAnimationType::walking;
        p.action=PeepActionType::idle; p.animationImageIdOffset=0; p.state=PeepState::walking; p.orientation=orientation;
        p.setTShirtColour(static_cast<Colour>(12)); p.setTrousersColour(static_cast<Colour>(6));
        const auto bounds=animations->GetSpriteBounds(group);
        p.spriteData.width=bounds.spriteWidth; p.spriteData.heightMin=bounds.spriteHeightNegative; p.spriteData.heightMax=bounds.spriteHeightPositive;
        p.moveToAndUpdateSpatialIndex(pos);
    }
    inline std::vector<std::byte> Paint(const std::array<int32_t,4>& target,uint8_t rotation,bool stable,json_t& trace)
    {
        struct Restore { bool stable=gPaintStableSort; ~Restore(){gPaintStableSort=stable;} } restore;
        gPaintStableSort=stable;
        X8DrawingEngine engine(GetContext()->GetUiContext()); engine.BeginDraw();
        struct End { X8DrawingEngine& engine; ~End(){engine.EndDraw();} } end{engine};
        std::vector<PaletteIndex> pixels(size_t(target[2])*target[3],PaletteIndex::transparent);
        const int32_t left=target[0]&~31,right=target[0]+target[2];
        for(int32_t x=left;x<right;x+=32)
        {
            RenderTarget rt{}; rt.x=std::max(x,target[0]); rt.y=target[1]; rt.width=std::min(right,x+32)-rt.x;
            rt.height=target[3]; rt.pitch=target[2]-rt.width; rt.bits=pixels.data()+rt.x-target[0]; rt.DrawingEngine=&engine;
            rt.cullingX=x; rt.cullingWidth=32;
            constexpr int32_t vertical=ZoomLevel::max().ApplyInversedTo(std::numeric_limits<int32_t>::max())/2;
            rt.cullingY=-vertical; rt.cullingHeight=vertical*2;
            std::unique_ptr<PaintSession,void(*)(PaintSession*)> session(PaintSessionAlloc(rt,0,rotation),PaintSessionFree);
            Require(bool(session),"No frozen fixture paint session");
            // Fresh owner-state diagnostic sessions bypass any prior presentation
            // publisher. These pointers are deliberately null, not a stale frame.
            session->EntitySnapshot=nullptr; session->MapSnapshot=nullptr;
            PaintSessionGenerate(*session); PaintSessionArrange(*session);
            json_t parents=json_t::array();
            for(const auto* parent=session->PaintHead;parent;parent=parent->NextQuadrantEntry)
            {
                json_t components=json_t::array();uint32_t children=0;
                for(const auto* part=parent;part;part=part->Children)
                {
                    Require(++children<=16,"Frozen component chain exceeds bounded diagnostic");
                    const auto& b=part->Bounds;
                    components.push_back({{"image",part->image_id.GetIndex()},{"screen",{part->ScreenPos.x,part->ScreenPos.y}},
                        {"bounds",{b.x,b.y,b.z,b.x_end,b.y_end,b.z_end}}});
                }
                parents.push_back(components);
            }
            trace.push_back({{"column",x},{"orderedParents",parents}});
            PaintDrawStructs(*session);
        }
        std::vector<std::byte> result(pixels.size()); std::memcpy(result.data(),pixels.data(),pixels.size()); return result;
    }
    inline void Capture(const std::filesystem::path& output,const std::string& oracleReceipt)
    {
        auto& state=getGameState(); auto& objects=GetContext()->GetObjectManager();
        // Locate authored donors once in loaded EverythingPark before resetting.
        std::optional<PathElement> pathDonor; std::optional<ObjectEntryIndex> rideSubtype; std::optional<SmallSceneryElement> treeDonor;
        for(const auto& ride:state.rides) if(ride.id!=RideId::GetNull() && ride.type==RIDE_TYPE_TWISTER_ROLLER_COASTER)
        { rideSubtype=ride.subtype; break; }
        for(int32_t y=1;y<state.mapSize.y-1;++y) for(int32_t x=1;x<state.mapSize.x-1;++x)
        {
            if(!pathDonor) for(const auto* p:TileElementsView<PathElement>(TileCoordsXY{x,y}))
                if(!p->isQueue() && !p->isSloped() && p->getSurfaceDescriptor()) {pathDonor=*p;break;}
            if(!treeDonor) for(const auto* tree:TileElementsView<SmallSceneryElement>(TileCoordsXY{x,y}))
            {
                const auto* object=objects.GetLoadedObject<SmallSceneryObject>(tree->getEntryIndex());
                if(object && object->GetIdentifier()=="rct2.scenery_small.tsb") {treeDonor=*tree;break;}
            }
        }
        Require(pathDonor && rideSubtype && treeDonor,"EverythingPark must supply ordinary path, birch and Twister ride");
        const uint32_t pathBase=pathDonor->getSurfaceDescriptor()->image;
        const auto treeEntry=*treeDonor->getEntry();
        const auto grass=objects.GetLoadedObjectEntryIndex("rct2.terrain_surface.grass");
        const auto edge=objects.GetLoadedObjectEntryIndex("rct2.terrain_edge.rock");
        auto* surfaceObject=objects.GetLoadedObject<TerrainSurfaceObject>(grass);
        Require(surfaceObject && edge!=kObjectEntryIndexNull,"Missing fixture terrain");
        const auto guestObject=findPeepAnimationsIndexForType(AnimationPeepType::guest);
        const auto staffObject=findPeepAnimationsIndexForType(AnimationPeepType::handyman);
        Require(guestObject!=kObjectEntryIndexNull && staffObject!=kObjectEntryIndexNull,"Missing fixture peep objects");
        gameStateInitAll(state,{32,32}); EntityTweener::get().reset(); MapAnimations::ClearAll();
        Config::Get().general.landscapeSmoothing=false;
        // All terrain is level. The 14x14 resident window surrounds a small
        // 192x128 target with a generous collar; omitted surfaces cannot touch it.
        std::vector<Mixed::StaticInstance> statics; std::vector<Mixed::StaticDefinition> definitions;
        auto append=[&](const auto& rows){const uint32_t first=static_cast<uint32_t>(definitions.size());
            definitions.insert(definitions.end(),rows.begin(),rows.end()); return first;};
        std::array<uint32_t,4> terrainDefinitions{};
        for(uint32_t variation=0;variation<4;++variation)
        {
            std::array<uint32_t,4> images{};
            for(uint32_t r=0;r<4;++r) images[r]=surfaceObject->GetImageId({int32_t(variation&1)*32,int32_t(variation>>1)*32},1,r,0,false,false).GetIndex();
            terrainDefinitions[variation]=append(MixedFixtureRecipes::FlatTerrain(variation+1,1,images));
        }
        for(int32_t y=0;y<32;++y) for(int32_t x=0;x<32;++x)
        {
            auto* s=MapGetFirstElementAt(TileCoordsXY{x,y})->asSurface(); Require(s,"Reset did not create surfaces");
            s->setBaseZ(32); s->setClearanceZ(32); s->setSlope(0); s->setWaterHeight(0); s->setGrassLength(1);
            s->setSurfaceObjectIndex(grass); s->setEdgeObjectIndex(edge); s->setOwnership(kUnowned); s->setParkFences(0);
            if(x>=9 && x<=22 && y>=9 && y<=22)
            {
                const uint32_t v=(x&1)|((y&1)<<1);
                statics.push_back({x*32,y*32,32,uint32_t(statics.size()),terrainDefinitions[v],v+1,1,1});
            }
        }
        const auto pathDefinition=append(MixedFixtureRecipes::StraightPath(10,1,pathBase,5));
        for(int32_t x=14;x<=17;++x)
        {
            const auto* added=InsertTileElement<PathElement>({x*32,15*32,32},15,[&](PathElement& p){
                p=*pathDonor; p.setBaseZ(32);p.setClearanceZ(48);p.setIsQueue(false);p.setSloped(false);p.setEdgesAndCorners(5);
                p.setAddition(0);p.setHasQueueBanner(false);p.setWide(false);p.setJunctionRailings(false);p.setGhost(false);});
            Require(added,"Could not insert fixture path"); statics.push_back({x*32,15*32,32,uint32_t(statics.size()),pathDefinition,10,1,1});
        }
        const auto treeDefinition=append(MixedFixtureRecipes::Tree(11,1,treeEntry,0,0));
        Require(InsertTileElement<SmallSceneryElement>({15*32,16*32,32},15,[&](SmallSceneryElement& tree){
            tree.setEntryIndex(treeDonor->getEntryIndex());tree.setBaseZ(32);tree.setClearanceZ((32+treeEntry.height+7)&~7);
            tree.setDirection(0);tree.setSceneryQuadrant(0);tree.setAge(0);}),"Could not insert fixture tree");
        statics.push_back({15*32,16*32,32,uint32_t(statics.size()),treeDefinition,11,1,1});
        auto& ride=state.rides[0]; ride.id=RideId::FromUnderlying(0);ride.type=RIDE_TYPE_TWISTER_ROLLER_COASTER;
        ride.subtype=*rideSubtype;state.ridesEndOfUsedRange=1;
        for(auto& colour:ride.trackColours) {colour.main=static_cast<Colour>(4);colour.additional=static_cast<Colour>(8);colour.supports=static_cast<Colour>(12);}
        const auto trackDefinition=append(MixedFixtureRecipes::RaisedTwisterFlat(12,1,0,5u|(9u<<8)|(2u<<24),2,13u|(9u<<8)|(2u<<24),2));
        Require(InsertTileElement<TrackElement>({16*32,16*32,64},15,[&](TrackElement& track){
            track.setTrackType(TrackElemType::flat);track.setRideIndex(ride.id);track.setRideType(ride.type);track.setDirection(0);
            track.setSequenceIndex(0);track.setClearanceZ(80);track.setHasChain(false);}),"Could not insert fixture track");
        statics.push_back({16*32,16*32,32,uint32_t(statics.size()),trackDefinition,12,1,1});
        auto* g0=state.entities.createEntity<Guest>(); auto* g1=state.entities.createEntity<Guest>();auto* staff=state.entities.createEntity<Staff>();
        Require(g0 && g1 && staff,"Could not allocate fixture peeps"); staff->assignedStaffType=StaffType::handyman;
        Pose(*g0,guestObject,PeepAnimationGroup::hat,{503,500,32},0);
        Pose(*g1,guestObject,PeepAnimationGroup::balloon,{527,519,32},16);
        Pose(*staff,staffObject,PeepAnimationGroup::normal,{520,511,32},8);
        for(auto* guest:{g0,g1}) {guest->setHatColour(static_cast<Colour>(14));guest->setBalloonColour(static_cast<Colour>(2));guest->setUmbrellaColour(static_cast<Colour>(20));}
        state.entities.resetEntitySpatialIndices(); EntityTweener::get().reset();
        state.currentTicks=0;state.date={};state.weatherCurrent={Weather::Type::sunny,20,Weather::EffectType::none,0,Weather::Level::none};
        state.weatherNext=state.weatherCurrent;
        std::vector<Descriptor> descriptors;std::vector<Fact> facts;auto images=MixedFixtureRecipes::StaticImages(definitions);
        for(uint32_t slot:std::set<uint32_t>{guestObject,staffObject})
        {
            const auto* object=objects.GetLoadedObject<PeepAnimationsObject>(slot);
            Require(object && object->GetBaseImageId()!=kImageIndexUndefined && object->GetNumImages()>0
                && object->GetNumAnimationGroups()>0 && object->GetNumAnimationGroups()<=256,"Peep object is not loaded after reset");
            Descriptor d{slot,1,uint32_t(object->GetNumAnimationGroups()),uint32_t(facts.size()),object->GetBaseImageId(),object->GetNumImages(),0,0};
            Require(d.count<=UINT32_MAX-d.base,"Loaded peep image extent overflows");
            facts.resize(facts.size()+d.groups*37);
            for(uint32_t group=0;group<d.groups;++group) for(const auto& [name,type]:getAnimationsByPeepType(object->GetPeepType()))
            {
                static_cast<void>(name);const auto& a=object->GetPeepAnimation(static_cast<PeepAnimationGroup>(group),type);
                Require(uint32_t(type)<37 && a.imageTableOffset<d.count && a.baseImage>=a.imageTableOffset
                    && a.baseImage-a.imageTableOffset==d.base,"Invalid or stale loaded peep object image base");
                facts[d.offset+group*37+uint32_t(type)]={a.baseImage,1,0,0};
            }
            // Assets cover every frame/direction of all walking groups admitted by
            // this fixture. Whole dense catalog facts above remain unchanged.
            for(uint32_t group:slot==guestObject?std::vector<uint32_t>{5,7,15}:std::vector<uint32_t>{0})
            {
                Require(group<d.groups,"Fixture animation group unavailable");
                const auto& a=object->GetPeepAnimation(static_cast<PeepAnimationGroup>(group),PeepAnimationType::walking);
                auto addFrame=[&](uint32_t frame){for(uint32_t dir=0;dir<4;++dir) {
                    const uint64_t image=uint64_t(a.baseImage)+frame*4+dir;
                    Require(image>=d.base && image<uint64_t(d.base)+d.count,"Fixture walking frame outside held object");
                    images.insert(static_cast<uint32_t>(image)); }};
                addFrame(0);for(auto frame:a.frameOffsets) addFrame(frame);
            }
            descriptors.push_back(d);
        }
        for(uint32_t base:{kPeepSpriteHatItemStart,kPeepSpriteBalloonItemStart,kPeepSpriteUmbrellaItemStart})
            for(uint32_t i=0;i<32;++i) images.insert(base+i);
        std::array<std::byte,65536> remap{};std::array<std::byte,1024> palette{};LoadPalette();
        for(uint32_t row=0;row<256;++row) for(uint32_t i=0;i<256;++i) remap[row*256+i]=std::byte(i);
        for(uint32_t colour=0;colour<54;++colour)
        { const auto map=GetPaletteMapForColour(static_cast<FilterPaletteID>(colour));Require(map.has_value(),"Missing ordinary recolour map");
          for(uint32_t i=0;i<256;++i) remap[(colour+1)*256+i]=std::byte(static_cast<uint8_t>((*map)[i])); }
        for(uint32_t i=0;i<256;++i) {palette[i*4]=std::byte(gPalette[i].red);palette[i*4+1]=std::byte(gPalette[i].green);
            palette[i*4+2]=std::byte(gPalette[i].blue);palette[i*4+3]=std::byte{255};}
        MixedFixtureCapture::Begin(output,{{"zoom",0},{"clearIndex",0},{"categories",{"terrain","path","tree","track","support","peep"}},
            {"oracleSourceReceiptSha256",oracleReceipt},{"recipe","finite-mixed-v1"},{"sortPolicies",{false,true}},
            {"entitySnapshot","null-fresh-owner-state"},{"terrainWindow",{9,9,22,22}}},images,std::as_bytes(std::span(statics)),
            std::as_bytes(std::span(definitions)),std::as_bytes(std::span(descriptors)),std::as_bytes(std::span(facts)),remap,palette);
        for(uint32_t phase=0;phase<2;++phase) for(uint32_t rotation=0;rotation<4;++rotation) for(bool stable:{false,true})
        {
            Pose(*g0,guestObject,phase==0?PeepAnimationGroup::hat:PeepAnimationGroup::umbrella,{503+int32_t(phase)*12,500,32},uint8_t(phase*8));
            const auto centre=Translate3DTo2DWithZ(rotation,{512,512,64});
            const std::array<int32_t,4> target={centre.x-96,centre.y-64,192,128};
            // Culling collar is checked with the real material metadata for all
            // omitted tiles; this is fixture admission, never a per-frame stream.
            for(int32_t y=0;y<32;++y) for(int32_t x=0;x<32;++x) if(x<9 || x>22 || y<9 || y>22)
            {
                auto tile=CoordsXY{x*32,y*32};if(rotation==1 || rotation==2) tile.x+=32;if(rotation==2 || rotation==3) tile.y+=32;
                const auto screen=Translate3DTo2DWithZ(rotation,{tile,32});
                const auto image=surfaceObject->GetImageId({x*32,y*32},1,rotation,0,false,false);const auto* a=GfxGetG1Element(image);
                Require(a && (screen.x+a->xOffset+a->width<=target[0] || screen.x+a->xOffset>=target[0]+192
                    || screen.y+a->yOffset+a->height<=target[1] || screen.y+a->yOffset>=target[1]+128),"Terrain collar too small");
            }
            const std::array raw={Read(*g0),Read(*g1),Read(*staff)};
            json_t trace=json_t::array();const auto pixels=Paint(target,uint8_t(rotation),stable,trace);
            const std::string name="phase"+std::to_string(phase)+"-r"+std::to_string(rotation)+(stable?"-stable":"-legacy");
            MixedFixtureCapture::AddCase(output,name,rotation,target,std::as_bytes(std::span(raw)),pixels);
            Json::WriteToFile((output/(name+".paint.json")).string(),{{"oracleOnly",true},{"gpuInput",false},
                {"rotation",rotation},{"stableSort",stable},{"columns",trace}});
            Require(state.currentTicks==0,"Frozen fixture advanced simulation");
        }
    }
}
