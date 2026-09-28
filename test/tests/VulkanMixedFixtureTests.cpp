// Experimental raster qualification; selected explicitly by run-mixed-parity.py.
#include <gtest/gtest.h>
#ifdef ENABLE_VULKAN
#include "VulkanParityTestSupport.h"
#include "../mixed-parity/VulkanMixedFixturePipeline.h"
#include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace
{
    namespace Vulkan=OpenRCT2::Ui::Vulkan;
    namespace Gpu=OpenRCT2::Ui::Gpu;
    namespace Mixed=Gpu::MixedFixture;
    void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
    template<typename T> std::vector<T> ReadBinary(const std::filesystem::path& path)
    {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        Require(file.good(),"Missing mixed fixture binary"); const auto bytes=file.tellg();
        Require(bytes>=0 && uint64_t(bytes)%sizeof(T)==0 && bytes<=64*1024*1024,"Bad mixed fixture binary size");
        std::vector<T> result(static_cast<size_t>(bytes)/sizeof(T)); file.seekg(0);
        if(bytes!=0) file.read(reinterpret_cast<char*>(result.data()),bytes);
        Require(file.good(),"Short mixed fixture binary"); return result;
    }
    std::vector<std::byte> DecodeHex(const std::string& value)
    {
        Require(value.size()%2==0,"Odd asset hex length");
        auto nibble=[](char c)->uint8_t { if(c>='0' && c<='9') return c-'0'; if(c>='a' && c<='f') return c-'a'+10;
            throw std::runtime_error("Invalid asset hex"); };
        std::vector<std::byte> out(value.size()/2);
        for(size_t i=0;i<out.size();++i) out[i]=std::byte((nibble(value[i*2])<<4)|nibble(value[i*2+1]));
        return out;
    }
    std::filesystem::path ContainedFile(const std::filesystem::path& root,const std::string& name)
    {
        const std::filesystem::path path(name);
        Require(!name.empty() && name!="." && name!=".." && path==path.filename(),"Fixture path must be a filename");
        return root/path;
    }
    void Write(const std::filesystem::path& path,std::span<const std::byte> bytes)
    {
        std::ofstream file(path,std::ios::binary); file.exceptions(std::ios::badbit|std::ios::failbit);
        file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    }
    struct Asset { int32_t width{},height{},x{},y{}; uint32_t index{}; std::vector<std::byte> pixels; };
    struct Case { std::string name; uint32_t rotation{}; std::array<int32_t,4> target{};
        std::vector<Gpu::Peeps::PeepRaw> peeps; std::vector<std::byte> expected; uint64_t expectedHash{}; };
    struct Corpus
    {
        json_t manifest;
        std::vector<Mixed::StaticInstance> statics;
        std::vector<Mixed::StaticDefinition> definitions;
        std::vector<Gpu::Peeps::PeepAnimationDescriptor> descriptors;
        std::vector<Gpu::Peeps::PeepAnimationFact> facts;
        std::vector<Gpu::Terrain::DrawSpriteMetadata> sprites;
        std::map<uint32_t,Asset> assets;
        std::vector<std::byte> remap;
        std::array<std::byte,1024> palette{};
        std::vector<Case> cases;
        uint64_t manifestHash{};
        explicit Corpus(const std::filesystem::path& root)
        {
            std::ifstream input(root/"corpus.json"); Require(input.good(),"Missing mixed corpus manifest"); input>>manifest;
            Require(manifest.at("schema")==1 && manifest.at("oracle")=="frozen-software-viewport"
                && manifest.at("zoom")==0 && manifest.at("clearIndex")==0,"Unqualified mixed corpus schema/policy");
            // The producer must bind provenance to an immutable old-painter source
            // receipt. This label alone is not a receipt verifier; runner owns it.
            Require(!manifest.at("oracleSourceReceiptSha256").get<std::string>().empty(),"Missing frozen oracle receipt");
            const auto categories=manifest.at("categories").get<std::set<std::string>>();
            Require(categories==std::set<std::string>{"terrain","path","tree","track","support","peep"},"Missing mixed fixture categories");
            statics=ReadBinary<Mixed::StaticInstance>(root/"statics.bin");
            definitions=ReadBinary<Mixed::StaticDefinition>(root/"definitions.bin");
            descriptors=ReadBinary<Gpu::Peeps::PeepAnimationDescriptor>(root/"descriptors.bin");
            facts=ReadBinary<Gpu::Peeps::PeepAnimationFact>(root/"facts.bin");
            remap=ReadBinary<std::byte>(root/"remap.bin"); const auto colours=ReadBinary<std::byte>(root/"palette.bin");
            Require(remap.size()==65536 && colours.size()==palette.size(),"Bad captured palette tables");
            std::copy(colours.begin(),colours.end(),palette.begin()); manifestHash=VulkanParitySupport::HashFile(root/"corpus.json");
            std::map<uint32_t,json_t> sourceAssets;
            for(const auto& a:manifest.at("assets")) Require(sourceAssets.emplace(a.at("image").get<uint32_t>(),a).second,"Duplicate asset image");
            int32_t x=0,y=0,row=0;
            for(const auto& [image,a]:sourceAssets)
            {
                const int32_t width=a.at("width"),height=a.at("height"); const uint32_t flags=a.at("flags");
                Require(width>0 && width<=2048 && height>0 && height<=2048 && (flags&~55u)==0
                    && a.at("coveredZeroPixels")==0,"Unsupported mixed atlas asset");
                if(x+width>2048) { x=0; y+=row; row=0; }
                Require(y+height<=2048,"Mixed atlas layer capacity exceeded");
                Asset asset{width,height,x,y,static_cast<uint32_t>(assets.size()),DecodeHex(a.at("decodedIndexedHex"))};
                Require(asset.pixels.size()==size_t(width)*height,"Bad asset decoded byte count");
                Gpu::Terrain::DrawSpriteMetadata sprite{}; sprite.imageIndex=image; sprite.width=width; sprite.height=height;
                sprite.xOffset=a.at("xOffset"); sprite.yOffset=a.at("yOffset");
                sprite.variants[0]={width,height,sprite.xOffset,sprite.yOffset,asset.index,(flags&4u)!=0?1u:0u,0,0};
                sprite.variants[1]=sprite.variants[0]; sprites.push_back(sprite);
                assets.emplace(image,std::move(asset)); x+=width; row=std::max(row,height);
            }
            Require(!assets.empty() && !statics.empty() && !descriptors.empty() && !facts.empty(),"Empty mixed corpus");
            std::set<uint32_t> rotations; std::set<std::string> names;
            for(const auto& value:manifest.at("cases"))
            {
                Case c; c.name=value.at("name"); c.rotation=value.at("rotation"); c.target=value.at("target").get<decltype(c.target)>();
                Require(c.rotation<4 && c.target[2]>0 && c.target[2]<=4096 && c.target[3]>0 && c.target[3]<=4096
                    && names.insert(c.name).second,"Invalid mixed case identity or extent");
                ContainedFile(root,c.name); rotations.insert(c.rotation);
                c.peeps=ReadBinary<Gpu::Peeps::PeepRaw>(ContainedFile(root,value.at("peeps")));
                auto expectedPath=ContainedFile(root,value.at("expected")); c.expected=ReadBinary<std::byte>(expectedPath);
                c.expectedHash=VulkanParitySupport::HashFile(expectedPath);
                Require(!c.peeps.empty() && c.expected.size()==size_t(c.target[2])*c.target[3],"Bad frozen raster/peep case");
                if(!cases.empty()) Require(c.target[2]==cases[0].target[2] && c.target[3]==cases[0].target[3],"Case canvas dimensions changed");
                Require(std::any_of(c.expected.begin(),c.expected.end(),[](auto b){return b!=std::byte{0};}),"Empty oracle image");
                cases.push_back(std::move(c));
            }
            Require(rotations.size()==4,"Mixed corpus needs all four camera rotations");
        }
    };

    TEST(ExperimentalVulkanMixedFixtureTest, FrozenOriginalArtAllRotationsAndCameraOnlyZeroUpload)
    {
        const auto* corpusPath=std::getenv("OPENRCT2_MIXED_CORPUS");
        const auto* emission=std::getenv("OPENRCT2_MIXED_EMISSION_SPV");
        const auto* shaders=std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY");
        const auto* output=std::getenv("OPENRCT2_MIXED_ARTIFACTS");
        const auto* depthModel=std::getenv("OPENRCT2_MIXED_DEPTH_MODEL");
        Require(!depthModel || std::string_view(depthModel)=="plane-envelope-v1","Unknown mixed depth diagnostic");
        if(!corpusPath || !emission || !shaders || !output)
        {
            const auto* required=std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
            if(required && std::string_view(required)=="1") FAIL()<<"Mixed fixture requires frozen corpus, emitter, shaders and durable artifacts";
            GTEST_SKIP()<<"No mixed fixture qualification inputs";
        }
        Corpus corpus(corpusPath); const std::filesystem::path artifacts(output);
        Require(!std::filesystem::exists(artifacts),"Refusing to overwrite mixed raster evidence");
        std::filesystem::create_directories(artifacts);
        const uint32_t width=corpus.cases.front().target[2],height=corpus.cases.front().target[3];
        // Exactly one device/session, shared atlas, shared indexed raster pipeline.
        auto device=Vulkan::DeviceContext::CreateGraphicsOnly();
        Vulkan::SubmissionSlots slots(device,32*1024*1024,2);
        Vulkan::IndexedResources resources; resources.Initialise(*device,{width,height},false,2,1);
        Vulkan::MixedFixturePipeline pipeline; pipeline.Initialise(*device,resources,shaders,emission);
        std::vector<Gpu::Peeps::PeepRaw> previous; uint64_t peepRevision=0;
        // Every case is rendered twice: the second draw must reuse all resident
        // source buffers and still reproduce the independently frozen raster.
        for(size_t sample=0;sample<corpus.cases.size()*2;++sample)
        {
            const auto& c=corpus.cases[sample/2];
            const bool changed=previous.size()!=c.peeps.size() || (!previous.empty()
                && std::memcmp(previous.data(),c.peeps.data(),previous.size()*sizeof(previous[0]))!=0);
            if(changed || peepRevision==0) { previous=c.peeps; ++peepRevision; }
            Vulkan::MixedFixtureScene scene{1,{1,1,peepRevision,1,1,1},corpus.statics,corpus.definitions,
                c.peeps,corpus.descriptors,corpus.facts,corpus.sprites};
            const uint32_t frame=static_cast<uint32_t>(sample%2);
            OpenRCT2::Drawing::RenderUploadTelemetry telemetry;
            auto token=slots.Begin(frame,true,&telemetry); Require(token.has_value(),"No mixed submission slot");
            if(sample==0)
            {
                auto remap=token->upload->Allocate(corpus.remap.size(),4); Require(bool(remap),"No remap upload space");
                std::memcpy(remap.data,corpus.remap.data(),corpus.remap.size()); remap.RecordHostWrite();
                resources.RecordIndexTableUpload(token->commandBuffer,remap,false);
                resources.BeginAtlasUploads(token->commandBuffer);
                for(const auto& [image,a]:corpus.assets)
                {
                    static_cast<void>(image); auto pixels=token->upload->Allocate(a.pixels.size(),4); Require(bool(pixels),"No atlas upload space");
                    std::memcpy(pixels.data,a.pixels.data(),a.pixels.size()); pixels.RecordHostWrite();
                    resources.RecordAtlasUpload(token->commandBuffer,pixels,0,{a.x,a.y,a.x+a.width,a.y+a.height},a.width);
                    const Gpu::SpriteAssetDescriptor descriptor{{a.x,a.y},0,0};
                    auto upload=token->upload->Allocate(sizeof(descriptor),4); Require(bool(upload),"No atlas descriptor upload space");
                    std::memcpy(upload.data,&descriptor,sizeof(descriptor)); upload.RecordHostWrite();
                    resources.RecordSpriteDescriptorUpload(token->commandBuffer,upload,a.index);
                }
                resources.EndAtlasUploads(token->commandBuffer);
            }
            resources.RecordCanvasAndDepthClear(token->commandBuffer,frame,0);
            // Deterministic unused output guards in durable command artifacts.
            vkCmdFillBuffer(token->commandBuffer,pipeline.GetCommandBuffer().GetBuffer(),0,
                pipeline.GetCommandBuffer().GetSize(),0xa5a5a5a5u);
            Mixed::Camera camera{}; camera.x=c.target[0]; camera.y=c.target[1]; camera.width=width; camera.height=height;
            camera.rotation=c.rotation; camera.assetCount=static_cast<uint32_t>(corpus.assets.size()); camera.outputCapacity=Mixed::kCommandCapacity;
            pipeline.Record(*token,scene,camera);
            const uint64_t expectedUpload=sample==0 ? corpus.statics.size()*sizeof(Mixed::StaticInstance)
                +corpus.definitions.size()*sizeof(Mixed::StaticDefinition)+c.peeps.size()*sizeof(Gpu::Peeps::PeepRaw)
                +corpus.descriptors.size()*sizeof(Gpu::Peeps::PeepAnimationDescriptor)+corpus.facts.size()*sizeof(Gpu::Peeps::PeepAnimationFact)
                +corpus.sprites.size()*sizeof(Gpu::Terrain::DrawSpriteMetadata)
                : changed ? c.peeps.size()*sizeof(Gpu::Peeps::PeepRaw) : 0;
            EXPECT_EQ(pipeline.GetLastUploadBytes(),expectedUpload);
            const size_t pixels=size_t(width)*height,statusOffset=(pixels+15)&~size_t{15};
            const size_t commandsOffset=statusOffset+pipeline.GetStatusBuffer().GetSize();
            auto readback=token->upload->Allocate(commandsOffset+pipeline.GetCommandBuffer().GetSize(),16);
            Require(bool(readback),"No mixed readback space");
            const VkMemoryBarrier transfer{.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
            vkCmdPipelineBarrier(token->commandBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,1,&transfer,0,nullptr,0,nullptr);
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            Vulkan::RecordImageBarrier(token->commandBuffer,resources.GetIndexedCanvas(frame).GetImage(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,range,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            const VkBufferImageCopy copy{.bufferOffset=readback.offset,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
            vkCmdCopyImageToBuffer(token->commandBuffer,resources.GetIndexedCanvas(frame).GetImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
            Vulkan::RecordImageBarrier(token->commandBuffer,resources.GetIndexedCanvas(frame).GetImage(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,range,
                VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
            for(const auto& [buffer,offset]:std::array<std::pair<const Vulkan::Buffer*,size_t>,2>{{
                {&pipeline.GetStatusBuffer(),statusOffset},{&pipeline.GetCommandBuffer(),commandsOffset}}})
            {
                const VkBufferCopy copyBuffer{0,readback.offset+offset,buffer->GetSize()};
                vkCmdCopyBuffer(token->commandBuffer,buffer->GetBuffer(),readback.buffer,1,&copyBuffer);
            }
            const VkMemoryBarrier host{.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
            vkCmdPipelineBarrier(token->commandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
            slots.Submit(*token); Require(slots.Wait(*token,UINT64_MAX),"Mixed draw did not complete");
            resources.CommitFrameLayouts(); token->upload->Invalidate(readback.offset,readback.size);
            std::vector<std::byte> snapshot(readback.size); std::memcpy(snapshot.data(),readback.data,snapshot.size());
            uint32_t error{},emitted{}; VkDrawIndirectCommand indirect{};
            std::memcpy(&error,snapshot.data()+statusOffset,4); std::memcpy(&emitted,snapshot.data()+statusOffset+4,4);
            std::memcpy(&indirect,snapshot.data()+statusOffset+Mixed::kIndirectByteOffset,sizeof(indirect));
            EXPECT_EQ(error,0u); EXPECT_GT(emitted,0u); EXPECT_LE(emitted,Mixed::kCommandCapacity);
            EXPECT_EQ(indirect.vertexCount,4u); EXPECT_EQ(indirect.instanceCount,emitted);
            EXPECT_EQ(indirect.firstVertex,0u); EXPECT_EQ(indirect.firstInstance,0u);
            if(emitted<=Mixed::kCommandCapacity)
                EXPECT_TRUE(std::all_of(snapshot.begin()+commandsOffset+emitted*sizeof(Gpu::SpriteCommand),snapshot.end(),
                    [](std::byte value){return value==std::byte{0xa5};}));
            const std::string name=c.name+"-"+std::to_string(sample%2); const auto folder=artifacts/name;
            std::filesystem::create_directory(folder);
            Write(folder/"status.bin",{snapshot.data()+statusOffset,static_cast<size_t>(pipeline.GetStatusBuffer().GetSize())});
            Write(folder/"commands.bin",{snapshot.data()+commandsOffset,static_cast<size_t>(pipeline.GetCommandBuffer().GetSize())});
            json_t metadata={{"rotation",c.rotation},{"zoom",0},{"runtimeAdmission",false},{"depthHypothesis",depthModel?depthModel:"rotated-bounds-origin-x+y+z"},
                {"uploadedWorldBytes",pipeline.GetLastUploadBytes()},{"expectedUploadBytes",expectedUpload},{"frameIndex",frame},
                {"gpuError",error},{"gpuComponents",emitted},{"corpusManifestFnv1a64",corpus.manifestHash},
                {"oracleIndexedFnv1a64",c.expectedHash},{"oracleSourceReceiptSha256",corpus.manifest.at("oracleSourceReceiptSha256")}};
            const std::span<const std::byte> actual(snapshot.data(),pixels);
            EXPECT_EQ(VulkanParitySupport::CompareAndReport(artifacts,name,"indexed",c.expected,actual,1,corpus.palette,metadata,{width,height}),0u);
            const auto expectedRgba=VulkanParitySupport::Expand(c.expected,corpus.palette),actualRgba=VulkanParitySupport::Expand(actual,corpus.palette);
            EXPECT_EQ(VulkanParitySupport::CompareAndReport(artifacts,name,"rgba",expectedRgba,actualRgba,4,corpus.palette,metadata,{width,height}),0u);
        }
    }
}
#endif
