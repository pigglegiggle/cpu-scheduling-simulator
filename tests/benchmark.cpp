#include "das/scheduler.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
volatile std::uint64_t checksum=0;
struct Mutation {std::size_t id;das::Key key;bool replace;};
double quantile(std::vector<double> values,double p){std::sort(values.begin(),values.end());return values[static_cast<std::size_t>(std::ceil(p*values.size()))-1];}
int main(){try{
    std::cout<<"{\"seed\":20261101,\"operations\":1000,\"repetitions\":7,\"sizes\":[";
    bool comma=false;
    for(std::size_t n:{1000,10000,100000}){
        std::mt19937_64 rng(20261101);std::vector<das::Entry> initial;std::vector<Mutation> operations;
        for(std::size_t i=0;i<n;++i)initial.push_back({i,{static_cast<std::int64_t>(rng()%1000000000),i}});
        std::set<std::pair<std::int64_t,std::size_t>> oracle;
        auto current=initial;
        for(const auto& e:initial)oracle.insert({e.key.rank,e.id});
        for(int i=0;i<1000;++i){
            auto id=i%4==0?oracle.begin()->second:rng()%n;
            auto rank=i%4==0?1000000000LL+i:static_cast<std::int64_t>(rng()%1000000000);
            operations.push_back({id,{rank,id},rng()%3==0});
            oracle.erase({current[id].key.rank,id});current[id].key.rank=rank;oracle.insert({rank,id});
        }
        auto indexed=[&](std::vector<std::size_t>& results){das::IndexedMinHeap heap(n);for(const auto& e:initial)heap.insert(e.id,e.key);
            auto start=Clock::now();for(std::size_t i=0;i<operations.size();++i){const auto& o=operations[i];
                if(o.replace){heap.erase(o.id);heap.insert(o.id,o.key);}else heap.update(o.id,o.key);results[i]=heap.top().id;}
            auto us=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
            if(!heap.valid())throw std::runtime_error("heap invariant failure");return us/operations.size();};
        auto scanned=[&](std::vector<std::size_t>& results){auto entries=initial;auto start=Clock::now();
            for(std::size_t i=0;i<operations.size();++i){const auto& o=operations[i];entries[o.id].key=o.key;
                auto best=std::min_element(entries.begin(),entries.end(),[](const auto& a,const auto& b){return das::before(a,b);});results[i]=best->id;}
            return std::chrono::duration<double,std::micro>(Clock::now()-start).count()/operations.size();};
        std::vector<std::size_t> a(operations.size()),b(operations.size());indexed(a);scanned(b);
        if(a!=b)throw std::runtime_error("minimum results disagree");
        std::vector<double> heap_times,scan_times;
        for(int repetition=0;repetition<7;++repetition){if(repetition%2){scan_times.push_back(scanned(b));heap_times.push_back(indexed(a));}
            else{heap_times.push_back(indexed(a));scan_times.push_back(scanned(b));}if(a!=b)throw std::runtime_error("timed results disagree");
            for(auto value:a)checksum+=value;}
        std::vector<das::Job> jobs;std::vector<das::Action> actions;
        for(std::size_t i=0;i<n;++i){auto release=static_cast<das::Tick>(i%100);
            jobs.push_back({"job-"+std::to_string(i),release,1,release+100+static_cast<das::Tick>(i%1000),static_cast<std::int64_t>(i%16)});
            if(i%10==0)actions.push_back({50,jobs.back().id,das::ActionKind::Cancel});}
        auto start=Clock::now();auto result=das::simulate(jobs,actions,das::Policy::EDF,false);
        auto scheduler_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if(result.metrics.completed+result.metrics.cancelled!=n)throw std::runtime_error("scheduler conservation failure");
        if(comma)std::cout<<',';comma=true;
        std::cout<<"{\"jobs\":"<<n<<",\"heap_us_per_operation_samples\":[";
        for(std::size_t i=0;i<heap_times.size();++i)std::cout<<(i?",":"")<<heap_times[i];
        std::cout<<"],\"scan_us_per_operation_samples\":[";
        for(std::size_t i=0;i<scan_times.size();++i)std::cout<<(i?",":"")<<scan_times[i];
        std::cout<<"],\"heap_median_us\":"<<quantile(heap_times,.5)<<",\"scan_median_us\":"<<quantile(scan_times,.5)
                 <<",\"results_verified\":true,\"scheduler_wall_ms\":"<<scheduler_ms<<",\"completed\":"<<result.metrics.completed
                 <<",\"cancelled\":"<<result.metrics.cancelled<<",\"ready_peak\":"<<result.ready_heap.peak<<"}";
    }
    std::cout<<"]}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
