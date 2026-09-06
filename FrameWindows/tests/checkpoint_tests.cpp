#include "AnalysisCache.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
using namespace fwl;
void require(bool ok,char const* message) {if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
int main() {
    std::vector<AnalysisTimeline> points(4);
    for(int i=0;i<4;++i)points[i].tick=(i+1)*240;
    require(selectAnalysisCheckpoint(points,751)==1,"latest safe checkpoint");
    require(selectAnalysisCheckpoint(points,752)==2,"inclusive safety boundary");
    require(!selectAnalysisCheckpoint(points,250),"no safe checkpoint falls back to start");
    require(!selectAnalysisCheckpoint(points,0),"first input falls back to start");
    require(selectAnalysisCheckpoint(points,752,40)==1,"larger maxWindow excludes unsafe checkpoint");
    require(selectAnalysisCheckpoint(points,752,20,40)==1,"larger safety excludes unsafe checkpoint");
    require(selectAnalysisCheckpoint(points,760)==selectAnalysisCheckpoint(points,761),"close inputs reuse snapshot");
    points[2].usable=false;
    require(selectAnalysisCheckpoint(points,760)==1,"unusable checkpoint falls back to earlier usable one");
    points[0].usable=points[1].usable=points[3].usable=false;
    require(!selectAnalysisCheckpoint(points,2000),"all unusable falls back to start");
    for(int duration: {1,240,7200,30000,3456000}) {
        int spacing=checkpointSpacing(duration);
        require((duration-1)/spacing<=checkpointCap,"spacing enforces snapshot cap");
        require(spacing>=180,"default spacing remains at least 0.75 seconds");
    }
    // Target-aware planning should put snapshots close to the safe boundary
    // before actual inputs and obey the hard cap even on a dense macro.
    Run planned;planned.endTick=7200;
    for(int t=80;t<7100;t+=35)planned.inputs.push_back({t,0,true});
    auto plan=inputAwareCheckpointPlan(planned);
    require(!plan.empty() && plan.size()<=checkpointCap,"input-aware plan obeys cap");
    require(std::is_sorted(plan.begin(),plan.end()),"input-aware plan is sorted");
    for(int tick:plan)require(tick>0 && tick<planned.endTick,"planned checkpoint is inside reference");
    int worstDistance=0;
    for(auto const& in:planned.inputs) {
        int safe=in.tick-maxWindow-checkpointSafetyTicks;
        if(safe<=0)continue;
        auto it=std::upper_bound(plan.begin(),plan.end(),safe);
        require(it!=plan.begin(),"every eligible dense input has a prior planned checkpoint");
        --it;worstDistance=std::max(worstDistance,safe-*it);
    }
    require(worstDistance<120,"dense target-aware plan stays much closer than one-second periodic cache");
    Run sparse;sparse.endTick=2000;sparse.inputs={{100,0,true},{1000,0,true},{1900,0,true}};
    auto sparsePlan=inputAwareCheckpointPlan(sparse);
    require(sparsePlan.size()==3 && sparsePlan[0]==68 && sparsePlan[1]==968 && sparsePlan[2]==1868,"small plans use exact latest-safe input anchors");

    Run held;held.inputs={{800,0,true},{820,1,true},{900,0,false},{920,1,false}};
    held.poses={{840},{850},{900}};
    auto cp=timelineAt(held,850);
    require(cp.inputCursor==2 && cp.poseCursor==1,"cursors address first unconsumed step");
    require(cp.jumpHeld[0] && cp.jumpHeld[1],"held input across checkpoint for both players");
    require(!timelineAt(held,901).jumpHeld[0] && timelineAt(held,901).jumpHeld[1],"independent dual held state");
    require(timelineAt(held,900).jumpHeld[0],"release at checkpoint tick is not consumed early");
    // Every merge (including simultaneous cross-player inputs) must exactly match
    // the old stable-sort implementation, from both zero and a safe checkpoint.
    std::mt19937 rng(47);
    for(int iteration=0;iteration<100;++iteration) {
        Run run;run.endTick=2000;
        for(int j=0;j<100;++j)run.inputs.push_back({int(rng()%1800),int(rng()%2),bool(rng()%2),float(j)});
        std::stable_sort(run.inputs.begin(),run.inputs.end(),[](auto const&a,auto const&b){return a.tick<b.tick;});
        for(size_t target=0;target<run.inputs.size();++target)for(int offset=-20;offset<=20;++offset) {
            if(!canShift(run,target,offset))continue;
            auto expected=shifted(run,target,offset);
            int start=std::max(0,run.inputs[target].tick-maxWindow-checkpointSafetyTicks);
            auto metadata=timelineAt(run,start);CandidatePlayback playback;
            playback.reset(metadata.inputCursor,target,offset);
            size_t i=0;while(i<expected.size() && expected[i].tick<start)++i;
            while(auto next=playback.next(run.inputs)) {
                require(i<expected.size(),"overlay size");
                require(playback.tick(run.inputs,*next)==expected[i].tick && run.inputs[*next].x==expected[i].x,"overlay matches stable sort from checkpoint");
                playback.consume(*next);++i;
            }
            require(i==expected.size(),"overlay delivers all remaining events exactly once");
        }
    }
    Search search(2);search.accept(true);
    auto changed=resolveFailureConfirmation(true,45,-1);
    require(!changed.stable && !changed.acceptedPass,"unstable failure remains conservative");
    search.results[0].unstable=true;search.accept(changed.acceptedPass,changed.acceptedFailureTick);
    require(search.stage==Search::Stage::Late,"unstable failure continues search");
    search.accept(false);search.accept(false);search.accept(false);
    require(search.stage==Search::Stage::FinalBaseline,"final baseline remains required");
    require(search.results[0].unstable && !search.results[1].unstable,"uncertainty belongs to affected input");
    search.accept(true);require(search.stage==Search::Stage::Done,"only final baseline completes analysis");
    std::cout<<"Checkpoint selection, timeline, overlay and search tests passed\n";
}
