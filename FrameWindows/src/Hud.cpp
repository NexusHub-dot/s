#include "Hud.hpp"
#include <Geode/utils/cocos.hpp>
using namespace geode::prelude;
namespace fwl {
namespace {
CCLabelBMFont* label(CCNode* parent,float scale=0.32f) {
    auto n=CCLabelBMFont::create("","chatFont.fnt"); n->setAnchorPoint({0,0.5f}); n->setScale(scale); parent->addChild(n); return n;
}
ccColor3B color(int n) { return Mod::get()->getSettingValue<ccColor3B>(fmt::format("fwl-color-{}",std::clamp(n,1,20))); }
ccColor4F rgba(ccColor3B c,float a=1) { return {c.r/255.f,c.g/255.f,c.b/255.f,a}; }
void ring(CCDrawNode* draw,CCPoint p,float radius,ccColor4F fill,ccColor4F stroke) {
    std::array<CCPoint,32> verts;
    for(int j=0;j<32;++j) { float a=static_cast<float>(j)*6.2831853f/32; verts[j]=CCPoint{p.x+std::cos(a)*radius,p.y+std::sin(a)*radius}; }
    draw->drawPolygon(verts.data(),32,fill,0.8f,stroke);
}
}
Hud* Hud::create(PlayLayer* pl) { auto h=new Hud; if(h->initWithLayer(pl)) {h->autorelease();return h;} delete h;return nullptr; }
bool Hud::initWithLayer(PlayLayer* pl) {
    if(!CCNode::init()) return false;
    setID("counter"_spr); setAnchorPoint({0,1});
    panel=CCDrawNode::create(); addChild(panel);
    title=label(this,0.72f); status=label(this,0.48f); totals=label(this,0.47f); footer=label(this,0.46f);
    for(auto& row:rows) row=label(this,0.60f);
    circles=CCDrawNode::create(); circles->setID("input-circles"_spr); pl->m_objectLayer->addChild(circles,1000);
    circleText=CCNode::create(); circleText->setID("input-circle-labels"_spr); pl->m_objectLayer->addChild(circleText,1001);
    scheduleUpdate(); return true;
}
void Hud::update(float dt) { refresh+=dt; if(refresh>=0.05f) {refresh=0;render();} }
void Hud::render() {
    auto& s=session(); auto mod=Mod::get();
    if(!s.layer || s.hud!=this) return;
    bool enabled=mod->getSettingValue<bool>("fwl-enabled");
    setVisible(enabled && (mod->getSettingValue<bool>("fwl-show-hud") || s.mode==Mode::Analyzing));
    circles->setVisible(enabled && mod->getSettingValue<bool>("fwl-show-circles")); circleText->setVisible(circles->isVisible());
    if(!enabled) return;
    auto screen=CCDirector::sharedDirector()->getWinSize();
    int lo=static_cast<int>(mod->getSettingValue<int64_t>("fwl-min-frame")),hi=static_cast<int>(mod->getSettingValue<int64_t>("fwl-max-frame"));
    if(lo>hi) std::swap(lo,hi);
    int n=hi-lo+1, columnRows=(n+1)/2;
    float height=78.f+columnRows*14.f, width=222;
    float scale=static_cast<float>(mod->getSettingValue<double>("fwl-hud-scale"));
    scale=std::min(scale,(screen.height-12)/height);
    setContentSize({width,height}); setScale(scale);
    float x=static_cast<float>(mod->getSettingValue<int64_t>("fwl-hud-x"));
    float y=static_cast<float>(mod->getSettingValue<int64_t>("fwl-hud-y"));
    setPosition({std::clamp(x,0.f,std::max(0.f,screen.width-width*scale)),std::clamp(screen.height-y,height*scale,screen.height)});
    panel->clear(); std::array<CCPoint,4> rect={CCPoint{0,0},{width,0},{width,height},{0,height}};
    panel->drawPolygon(rect.data(),4,{0.025f,0.035f,0.065f,mod->getSettingValue<int64_t>("fwl-panel-opacity")/255.f},0.6f,{0.3f,0.4f,0.6f,0.7f});
    title->setPosition({8,height-12}); title->setString("FRAME WINDOWS"); title->setColor({190,217,255});
    status->setPosition({8,height-27}); std::string msg=s.status;
    if(msg.size()>48) msg=msg.substr(0,45)+"..."; status->setString(msg.c_str()); status->limitLabelWidth(width-16,0.48f,0.25f);
    int tick=s.physicsTick;
    Run const* run=s.mode==Mode::Recording?&s.recording:(s.reference?&*s.reference:nullptr);
    bool releases=mod->getSettingValue<bool>("fwl-include-releases");
    int player=static_cast<int>(mod->getSettingValue<int64_t>("fwl-player-filter"));
    bool cumulative=mod->getSettingValue<bool>("fwl-cumulative");
    Counts counts; if(run) counts=count(*run,tick,releases,player);
    int outside=counts.above; for(int i=hi+1;i<=20;++i) outside+=counts.exact[i];
    totals->setPosition({8,height-41}); totals->setString(fmt::format("P {}  R {}   >{}: {}   ?: {}",counts.presses,counts.releases,hi,outside,counts.unknown).c_str()); totals->limitLabelWidth(width-16,0.47f,0.22f);
    if(s.mode==Mode::Analyzing) {
        auto progress=fmt::format("{} {} | trial {} ticks{}",s.activeCheckpoint?"Checkpoint":"Start",s.trialStartTick,
            std::max(0,s.physicsTick-s.trialStartTick),s.analysisUncertain?" | WARNING":"");
        totals->setString(progress.c_str());totals->limitLabelWidth(width-16,0.47f,0.22f);
    }
    for(auto row:rows) row->setVisible(false);
    int sum=0;
    for(int i=1;i<=hi;++i) {
        sum+=counts.exact[i]; if(i<lo) continue;
        int offset=i-lo, col=offset/columnRows, row=offset%columnRows;
        float xx=10.f+col*109.f, yy=height-57.f-row*14;
        ring(panel,{xx+3,yy},2.7f,rgba(color(i)),rgba(color(i)));
        auto text=rows[i-1]; text->setVisible(true); text->setColor(color(i)); text->setPosition({xx+11,yy});
        text->setString(fmt::format("{}{}f: {}",cumulative?"<=":"",i,cumulative?sum:counts.exact[i]).c_str());
    }
    footer->setPosition({8,10});
    std::string bottom=run && run->nextClick?"TO NEXT ACTION | 240 Hz":"FULL MACRO | 240 Hz";
    if(s.mode==Mode::Analyzing)bottom=analysisProgress();
    if(run && s.mode!=Mode::Analyzing && mod->getSettingValue<bool>("fwl-show-precision")) {
        if(tick<s.precisionTick || tick-s.precisionTick>=120 || s.precisionTick<0) {
            s.precisionValue=precision(*run,std::min(tick,run->endTick)).value_or(-1); s.precisionTick=tick;
        }
        if(s.precisionValue>=0) bottom=fmt::format("Base precision <= {:.2f} sigma/s",s.precisionValue);
    }
    footer->setString(bottom.c_str()); footer->limitLabelWidth(width-16,0.46f,0.25f);
    circles->clear(); circleText->removeAllChildrenWithCleanup(true);
    if(!run || !circles->isVisible() || s.mode==Mode::Analyzing) return;
    int lifetime=static_cast<int>(mod->getSettingValue<double>("fwl-circle-lifetime")*240);
    float radius=static_cast<float>(mod->getSettingValue<double>("fwl-circle-radius"));
    auto start=std::lower_bound(run->inputs.begin(),run->inputs.end(),tick-lifetime,[](auto const& in,int t){return in.tick<t;});
    int drawn=0;
    for(auto it=start;it!=run->inputs.end() && it->tick<=tick && drawn<100;++it) {
        size_t i=static_cast<size_t>(it-run->inputs.begin());
        if((!releases&&!it->down)||(player&&it->player+1!=player)) continue;
        Window w=i<run->windows.size()?run->windows[i]:Window{};
        ccColor3B c=w.measured&&!w.overflow?color(w.width()):ccColor3B{165,170,180};
        CCPoint p{it->x,it->y}; float alpha=std::clamp(1.f-static_cast<float>(tick-it->tick)/lifetime,0.1f,1.f);
        ring(circles,p,radius,rgba(c,it->down?alpha*0.2f:0),rgba(c,alpha));
        auto text=label(circleText,0.22f); text->setAnchorPoint({0.5f,0.5f}); text->setPosition(p); text->setColor(c); text->setOpacity(static_cast<GLubyte>(alpha*255));
        text->setString((w.measured?(w.overflow?std::string("21+"):std::to_string(w.width())):std::string("?")).c_str());
        ++drawn;
    }
}
}

