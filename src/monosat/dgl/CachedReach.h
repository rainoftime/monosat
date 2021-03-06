/****************************************************************************************[Solver.h]
 The MIT License (MIT)

 Copyright (c) 2017, Sam Bayless

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 **************************************************************************************************/


#ifndef MONOSAT_CACHED_REACH_H
#define MONOSAT_CACHED_REACH_H


#include <vector>
#include "monosat/dgl/alg/Heap.h"
#include "DynamicGraph.h"
#include "monosat/core/Config.h"
#include "Reach.h"
#include "alg/IntMap.h"

namespace dgl {

template<typename Weight,class Status = typename Distance<Weight>::NullStatus, bool undirected = false>
class CachedReach: public Reach {
public:

    DynamicGraph<Weight> & g;
    Status & status;
    int last_modification=-1;
    int last_addition=-1;
    int last_deletion=-1;
    int history_qhead=0;
    int reportPolarity=0;
    Reach * reach;
    int last_history_clear=0;



    long stats_full_updates=0;
    long stats_fast_updates=0;
    long stats_fast_failed_updates=0;
    long stats_skip_deletes=0;
    long stats_skipped_updates=0;
    long stats_num_skipable_deletions=0;


    double stats_full_update_time=0;
    double stats_fast_update_time=0;


public:


    CachedReach(Reach * reach,DynamicGraph<Weight> & graph,Status & status, int reportPolarity = 0) :
            g(graph),status(status), reach(reach),reportPolarity(reportPolarity){

    }
    CachedReach(Reach * reach, DynamicGraph<Weight> & graph,int reportPolarity = 0) :
            g(graph), status(Distance<Weight>::nullStatus),reportPolarity(reportPolarity){

    }

    void setSource(int s) {
        if(s!=reach->getSource()) {
            needs_recompute = true;
            last_modification = -1;
            last_addition = -1;
            last_deletion = -1;
            reach->setSource(s);
        }
    }
    int getSource() {
        return reach->getSource();
    }


    long num_updates = 0;
    int numUpdates() const {
        return num_updates;
    }


    alg::IntSet<int> destinations;
    alg::IntSet<int> edge_in_path;
    //std::vector<bool> edge_in_path;
    std::vector<bool> has_path_to;
    std::vector<int> previous_edge;
    bool has_non_reach_destinations=false;
    bool needs_recompute=true;
    void clearCache(){
        needs_recompute=true;
    }

    void addDestination(int node) override{
        if(!destinations.has(node)) {
            destinations.insert(node);
            has_non_reach_destinations=true;
        }
    }
    void removeDestination(int node) override{

    }
    void printStats(){
        reach->printStats();
    }
    void recompute(){
        needs_recompute=false;
        reach->update();
        //do not need to reset previous_edge vector here; instead we allow it to contain incorrect values on the assumption they will be corrected before being accessed
        edge_in_path.clear();//clear and rebuild the path tree
        int source = getSource();
        assert(previous_edge[source]==-1);
        has_non_reach_destinations=false;
        for(int d:destinations){

            if(d==getSource()){
                has_path_to[d]=true;
                continue;
            }

            if(reach->connected(d)){
                has_path_to[d]=true;
                if (reportPolarity >= 0) {
                    status.setReachable(d, true);
                }
                //extract path
                int edgeID = reach->incomingEdge(d);
                assert(edgeID>=0);
                int p = d;
                while(!edge_in_path.has(edgeID)){
                    assert(edgeID>=0);
                    previous_edge[p] = edgeID;//do not need to reset previous_edge vector here; instead we allow it to contain incorrect values on the assumption they will be corrected before being accessed

                    edge_in_path.insert(edgeID);
                    p = reach->previous(p);
                    if(p==source){
                        edgeID=-1;
                        break;
                    }
                    edgeID = reach->incomingEdge(p);
                    assert(edgeID>=0);
                }
                assert(previous_edge[p] == edgeID);
            }else{
                has_path_to[d]=false;
                previous_edge[d]=-1;
                has_non_reach_destinations=true;
                if (reportPolarity <= 0) {
                    status.setReachable(d, false);
                }
            }
        }

        num_updates++;
        last_modification = g.modifications;
        last_deletion = g.deletions;
        last_addition = g.additions;

        history_qhead = g.historySize();

        last_history_clear = g.historyclears;
    }
    void update() override{
        static int iteration = 0;
        int local_it = ++iteration;

        if (!needs_recompute && last_modification > 0 && g.modifications == last_modification){
            return;
        }


        if (last_modification <= 0 || g.changed()) {//Note for the future: there is probably room to improve this further.
            edge_in_path.clear();

            //edge_in_path.resize(g.edges(),false);
            has_path_to.clear();
            has_path_to.resize(g.nodes(),false);
            previous_edge.clear();
            previous_edge.resize(g.nodes(),-1);
            // prev_edge.clear();
            //prev_edge.resize(g.nodes(),-1);
            needs_recompute=true;

        }

        if (!needs_recompute && last_history_clear != g.historyclears) {
            if (!g.changed() && last_history_clear>=0 && last_history_clear == g.historyclears-1 && history_qhead==g.getPreviousHistorySize()){
                //no information was lost in the history clear
                history_qhead = 0;
                last_history_clear = g.historyclears;
            }else {
                history_qhead = g.historySize();
                last_history_clear = g.historyclears;
                for (int edgeid = 0; edgeid < g.edges(); edgeid++) {
                    if (g.edgeEnabled(edgeid)) {
                        if(has_non_reach_destinations){
                            //needs recompute
                            needs_recompute=true;
                            break;
                        }
                    } else if (edge_in_path.has(edgeid)){
                        //needs recompute
                        needs_recompute=true;
                        break;
                    }
                }
            }
        }
        if(!needs_recompute) {
            for (int i = history_qhead; i < g.historySize(); i++) {
                int edgeid = g.getChange(i).id;

                if (g.getChange(i).addition && g.edgeEnabled(edgeid)) {
                    int v = g.getEdge(edgeid).to;
                    if (has_non_reach_destinations) {
                        //needs recompute
                        needs_recompute = true;
                        break;
                    }
                } else if (!g.getChange(i).addition && !g.edgeEnabled(edgeid)) {
                    int v = g.getEdge(edgeid).to;
                    if (edge_in_path.has(edgeid)) {
                        needs_recompute = true;
                        break;
                    }
                }
            }
        }

        if(needs_recompute){
            recompute();

        }


        num_updates++;
        last_modification = g.modifications;
        last_deletion = g.deletions;
        last_addition = g.additions;

        history_qhead = g.historySize();

        last_history_clear = g.historyclears;

    }
    virtual bool dbg_manual_uptodate(){
        return reach->dbg_manual_uptodate();
    }
    bool connected_unsafe(int t) {
        return has_path_to[t];
    }
    bool connected_unchecked(int t) {
        assert(last_modification == g.modifications && ! needs_recompute);
        return connected_unsafe(t);
    }
    bool connected(int t) {
        //if (last_modification != g.modifications)
        update();
        return has_path_to[t];
    }
    int distance(int t) {
        if (connected(t))
            return 1;
        else
            return -1;
    }
    int distance_unsafe(int t) {
        if (connected_unsafe(t))
            return 1;
        else
            return -1;
    }
    int incomingEdge(int t) {
        assert(last_modification == g.modifications && ! needs_recompute);
        assert(previous_edge[t]>=0);
        return previous_edge[t]; //reach->incomingEdge(t);
    }
    int previous(int t) {
        assert(last_modification == g.modifications && ! needs_recompute);
        int edgeID = incomingEdge(t);
        assert(edgeID>=0);
        assert(g.edgeEnabled(edgeID));
        assert(g.getEdge(edgeID).to==t);
        return g.getEdge(edgeID).from;
    }

};
}
;

#endif //MONOSAT_CACHED_REACH_H
