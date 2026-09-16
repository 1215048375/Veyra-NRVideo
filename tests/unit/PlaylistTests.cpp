#include "veyra/engine/Playlist.h"
#include <iostream>
#include "../../apps/veyra/ui/ImageFolder.h"
#include <fstream>
#include <chrono>
#include "veyra/engine/ImageDecodeCache.h"
#include <atomic>
#include <thread>
#include <stdexcept>

int main() {
    unsigned checks = 0;
    auto require = [&](bool ok, const char* why) { ++checks;if (!ok) throw std::runtime_error(why); };
    try {
        using namespace veyra::engine;
        veyra::ui::ImageFolder images;
        auto folder=std::filesystem::path("out/playlist")/("image-folder-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder/"nested");
        for(auto name:{"c.jpeg","A.PNG","b.jpg","video.mp4","audio.wav"})std::ofstream(folder/name).put('x');
        std::ofstream(folder/"nested"/"ignored.png").put('x');
        std::error_code ec;require(images.load(folder,ec)&&images.files.size()==3,"folder includes only supported images and excludes subfolders");
        require(std::filesystem::path(images.files[0]).filename()==L"A.PNG","case-insensitive filename order");
        require(!images.wheel(-30,1000)&&!images.wheel(-30,1001)&&!images.wheel(-30,1002),"high-resolution wheel accumulates a notch");
        require(images.wheel(-30,1003)==1,"one notch advances one image");images.opened(images.files[1]);
        require(!images.wheel(-1200,1100),"wheel burst discarded during cooldown");
        require(images.wheel(-1200,1500)==2,"large wheel delta advances at most one image");images.opened(images.files[2]);
        require(!images.wheel(-120,2000),"last image does not wrap");
        require(images.wheel(120,2500)==1,"wheel up goes back");
        require(!images.load(folder/"nested"/"missing",ec)&&images.files.size()==3,"failed folder load preserves current gallery");
        images.opened(L"unrelated.mp4");require(images.files.empty(),"opening unrelated media leaves folder browser");
        {
            std::atomic<int> decoded=0;
            ImageDecodeCache cache([&](const std::wstring&,veyra::sink::RgbaImage& image,size_t limit){++decoded;if(limit<4)return false;image.width=image.height=1;image.pixels.resize(4);return true;},8);
            std::vector<std::wstring> paths={(folder/"A.PNG").wstring(),(folder/"b.jpg").wstring(),(folder/"c.jpeg").wstring()};
            cache.prefetch(paths);
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!cache.get(paths[1])&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            require(cache.get(paths[0])&&cache.get(paths[1])&&cache.bytes()==8,"background decoding retains images within budget");
            require(!cache.get(paths[2]),"budget excludes extra image");
            auto held=cache.get(paths[0]);cache.prefetch({});require(cache.bytes()==0&&!cache.get(paths[0])&&held->pixels.size()==4,"clear evicts cache without invalidating displayed shared image");
            cache.prefetch({paths[0]});deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!cache.get(paths[0])&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            require(bool(cache.get(paths[0])),"refill after clear");
            std::ofstream(folder/"A.PNG",std::ios::app).put('y');require(!cache.get(paths[0]),"changed source is never served stale");
        }
        {
            std::atomic<bool> started=false;
            auto old=(folder/"A.PNG").wstring(),next=(folder/"b.jpg").wstring();
            ImageDecodeCache cache([&](const std::wstring& path,veyra::sink::RgbaImage& image,size_t){if(path==old){started=true;std::this_thread::sleep_for(std::chrono::milliseconds(80));}image.width=image.height=1;image.pixels.resize(4);return true;});
            cache.prefetch({old});auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!started&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cache.prefetch({next});deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!cache.get(next)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            require(!cache.get(old)&&cache.get(next),"obsolete in-flight decode discarded after new window");
        }
        Playlist p;
        require(!p.adjacent(1) && !p.ended(1), "empty queue");
        require(!p.add(L"still.png") && !p.add(L"capture:device"), "non-video excluded");
        require(p.add(L"C:/媒体/第一集.MP4") == 0, "Unicode and uppercase extension");
        require(p.add(L"C:/媒体/第二集.mkv") == 1, "append order");
        require(p.add(L"C:/媒体/第三集.mov") == 2, "third episode");
        require(p.add(L"C:/媒体/第一集.MP4") == 0 && p.entries.size() == 3, "duplicate retains index");
        p.bind(0, 11);
        require(!p.ended(10), "stale EOF ignored");
        require(p.ended(11) == 1 && !p.ended(11), "EOF consumed once");
        p.bind(2, 12);
        require(!p.ended(12), "sequential stops at end");
        p.mode = PlaylistMode::Sequential;p.repeatAll=true;p.bind(2, 13);
        require(p.ended(13) == 0, "list loop wraps forward");
        p.bind(0, 14);require(p.adjacent(-1) == 2, "list loop wraps backward");
        p.mode = PlaylistMode::RepeatOne;
        require(p.ended(14) == 0 && p.adjacent(1) == 1, "repeat one does not trap manual navigation");
        p.bind(1, 15);p.move(1, 2);
        require(p.current == 2 && p.entries[2].find(L"第二集") != std::wstring::npos, "reorder tracks playing item");
        p.remove(0);require(p.current == 1, "remove before current adjusts index");
        p.remove(1);require(!p.current && !p.ended(15), "remove playing item prevents auto advance");
        p.bind(0, 16);p.detach();require(!p.ended(16), "source switch cancels queue EOF");
        p.bind(0, 16);p.suspend();require(p.current == 0 && !p.ended(16), "stop preserves navigation but disables auto advance");
        p.bind(0, 17);p.clear();require(p.entries.empty() && !p.ended(17), "clear while playing");
        p.add(L"one.mp4");p.mode=PlaylistMode::Sequential;p.repeatAll=true;p.bind(0, 18);
        require(p.ended(18) == 0 && !p.ended(18), "single-item list loops once per session");
        p.remove(100);p.move(0,100);require(p.entries.size() == 1, "invalid edits ignored");
        p.clear();
        for (size_t i=0;i<Playlist::limit;++i) p.add(std::to_wstring(i)+L".mp4");
        require(!p.add(L"overflow.mp4") && p.entries.size()==Playlist::limit, "bounded queue");
        p.clear();p.repeatAll=false;p.mode=PlaylistMode::Shuffle;
        for(int i=0;i<20;++i)p.add(std::to_wstring(i)+L".mp4");
        p.bind(0,100);std::vector<size_t> order{0};
        for(int i=1;i<20;++i){auto next=p.ended(99+i);require(next.has_value(),"shuffle continues until every entry visited");require(std::find(order.begin(),order.end(),*next)==order.end(),"shuffle has no repeats in a traversal");auto previous=*p.current;p.bind(*next,100+i);require(p.adjacent(-1)==previous,"shuffle previous retraces traversal");order.push_back(*next);}
        require(!p.ended(119),"shuffle stops without list loop");
        p.repeatAll=true;require(p.adjacent(1)==order.front(),"shuffle list loop wraps");
        p.mode=PlaylistMode::RepeatOne;p.repeatAll=false;p.bind(*p.current,120);require(p.ended(120)==p.current,"repeat one independent of list loop");
        p.mode=PlaylistMode::Shuffle;p.remove(*p.current);require(!p.current&&!p.ended(120),"shuffle removal detaches EOF");require(p.adjacent(1).has_value(),"shuffle safely rebuilds after edit");
        std::cout << "PASS " << checks << " playlist checks\n";return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n';return 1; }
}
