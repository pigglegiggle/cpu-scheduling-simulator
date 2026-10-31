#include "das/scheduler.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace das {
Policy parse_policy(const std::string& name) {
    if (name=="edf") return Policy::EDF;
    if (name=="fixed") return Policy::FixedPriority;
    if (name=="fifo") return Policy::FIFO;
    throw std::invalid_argument("policy must be edf, fixed, or fifo");
}
std::string policy_name(Policy p) { return p==Policy::EDF?"edf":p==Policy::FixedPriority?"fixed":"fifo"; }
std::string state_name(State s) {
    switch(s) { case State::Future:return "future"; case State::Ready:return "ready";
        case State::Completed:return "completed"; case State::Cancelled:return "cancelled"; }
    throw std::logic_error("invalid state");
}
void validate(const std::vector<Job>& jobs,const std::vector<Action>& actions) {
    if (jobs.empty() || jobs.size()>100000) throw std::invalid_argument("jobs must contain 1..100000 entries");
    if (actions.size()>100000) throw std::invalid_argument("at most 100000 actions");
    std::unordered_map<std::string,std::size_t> ids;
    for (std::size_t i=0;i<jobs.size();++i) {
        const auto& j=jobs[i];
        if (j.id.empty() || j.id.size()>80 || std::any_of(j.id.begin(),j.id.end(),[](unsigned char c){return c<33 || c>126;}))
            throw std::invalid_argument("job IDs must be 1..80 printable ASCII characters without spaces");
        if (!ids.emplace(j.id,i).second) throw std::invalid_argument("duplicate job ID: "+j.id);
        if (j.release<0 || j.release>max_input_time || j.execution<1 || j.execution>1000000000LL ||
            j.deadline<j.release || j.deadline>max_input_time || j.priority<0 || j.priority>1000000)
            throw std::invalid_argument("invalid time, execution, or priority for job: "+j.id);
    }
    for (const auto& a:actions) {
        auto found=ids.find(a.job);
        if (found==ids.end()) throw std::invalid_argument("action references unknown job: "+a.job);
        if (a.time<0 || a.time>max_input_time) throw std::invalid_argument("action time out of range");
        if (a.kind==ActionKind::Deadline && (a.value<jobs[found->second].release || a.value>max_input_time))
            throw std::invalid_argument("updated deadline must be >= release and within time limit");
        if (a.kind==ActionKind::Priority && (a.value<0 || a.value>1000000)) throw std::invalid_argument("priority out of range");
    }
}
Result simulate(const std::vector<Job>& jobs,const std::vector<Action>& actions,Policy policy,bool with_trace) {
    validate(jobs,actions);
    Result result; result.policy=policy;
    std::unordered_map<std::string,std::size_t> ids;
    for (std::size_t i=0;i<jobs.size();++i) { result.jobs.push_back({jobs[i],State::Future,jobs[i].execution}); ids.emplace(jobs[i].id,i); }
    std::vector<std::size_t> releases(jobs.size()),commands(actions.size());
    std::iota(releases.begin(),releases.end(),0); std::iota(commands.begin(),commands.end(),0);
    std::stable_sort(releases.begin(),releases.end(),[&](auto a,auto b){return jobs[a].release<jobs[b].release;});
    std::stable_sort(commands.begin(),commands.end(),[&](auto a,auto b){return actions[a].time<actions[b].time;});
    IndexedMinHeap ready(jobs.size()),deadlines(jobs.size());
    std::vector<std::uint64_t> order(jobs.size()); std::uint64_t sequence=0;
    auto key=[&](std::size_t id) { const auto& j=result.jobs[id].job;
        return Key{policy==Policy::EDF?j.deadline:policy==Policy::FixedPriority?j.priority:0,order[id]}; };
    auto emit=[&](Tick t,const std::string& type,std::int64_t job) {
        if (!with_trace) return;
        if (result.trace.size()>=1000000) throw std::runtime_error("trace limit exceeded");
        result.trace.push_back({t,type,job,ready.size(),ready.empty()?-1:static_cast<std::int64_t>(ready.top().id)});
    };
    std::size_t ri=0,ai=0; Tick now=0; std::int64_t previous=-1;
    const Tick infinity=std::numeric_limits<Tick>::max();
    while (ri<releases.size() || ai<commands.size() || !ready.empty()) {
        // Completion at exactly a deadline succeeds; then controls, releases, deadline checks, dispatch.
        if (previous>=0 && result.jobs[previous].state==State::Ready && result.jobs[previous].remaining==0) {
            auto& j=result.jobs[previous]; j.state=State::Completed; j.finish=now;
            ready.erase(previous); deadlines.erase(previous); emit(now,"complete",previous);
        }
        while (ai<commands.size() && actions[commands[ai]].time==now) {
            const auto& a=actions[commands[ai++]]; auto id=ids.at(a.job); auto& j=result.jobs[id];
            if (j.state==State::Completed || j.state==State::Cancelled) { emit(now,"ignored_action",id); continue; }
            if (a.kind==ActionKind::Cancel) {
                ready.erase(id); deadlines.erase(id); j.state=State::Cancelled; j.cancelled_at=now; emit(now,"cancel",id);
            } else if (a.kind==ActionKind::Deadline) {
                j.job.deadline=a.value;
                if (ready.contains(id)) ready.update(id,key(id));
                if (deadlines.contains(id)) deadlines.update(id,{a.value,order[id]});
                emit(now,"deadline_update",id);
            } else {
                j.job.priority=a.value; if (ready.contains(id)) ready.update(id,key(id)); emit(now,"priority_update",id);
            }
        }
        while (ri<releases.size() && jobs[releases[ri]].release==now) {
            auto id=releases[ri++]; auto& j=result.jobs[id]; if (j.state==State::Cancelled) continue;
            order[id]=sequence++; j.state=State::Ready;
            ready.insert(id,key(id)); deadlines.insert(id,{j.job.deadline,order[id]}); emit(now,"release",id);
        }
        while (!deadlines.empty() && deadlines.top().key.rank<=now) {
            auto id=deadlines.pop().id; result.jobs[id].missed=true; emit(now,"deadline_miss",id);
        }
        auto selected=ready.empty()?-1:static_cast<std::int64_t>(ready.top().id);
        if (selected!=previous && previous>=0 && result.jobs[previous].state==State::Ready && result.jobs[previous].remaining>0) {
            ++result.metrics.preemptions; emit(now,"preempt",previous);
        }
        if (selected>=0 && selected!=previous) { ++result.metrics.dispatches; emit(now,"dispatch",selected); }
        Tick next=infinity;
        if (ri<releases.size()) next=std::min(next,jobs[releases[ri]].release);
        if (ai<commands.size()) next=std::min(next,actions[commands[ai]].time);
        if (!deadlines.empty()) next=std::min(next,deadlines.top().key.rank);
        if (selected>=0) next=std::min(next,now+result.jobs[selected].remaining);
        if (next==infinity) break;
        if (next<=now) throw std::logic_error("simulation failed to advance");
        if (selected>=0) {
            auto& j=result.jobs[selected]; if (j.first_start<0) j.first_start=now;
            j.remaining-=next-now; j.executed+=next-now; result.metrics.busy+=next-now;
        }
             if (!result.timeline.empty() && result.timeline.back().job==selected && result.timeline.back().end==now)
            result.timeline.back().end=next;
        else result.timeline.push_back({now,next,selected});
        previous=selected; now=next;
    }
    auto& m=result.metrics; m.elapsed=now; m.idle=now-m.busy; m.utilization=now?static_cast<double>(m.busy)/now:0;
    std::size_t started=0;
    for (const auto& j:result.jobs) {
        if (j.missed) ++m.misses;
        if (j.first_start>=0) { ++started; m.mean_response+=j.first_start-j.job.release; }
        if (j.state==State::Cancelled) ++m.cancelled;
        if (j.state==State::Completed) {
            ++m.completed; if (!j.missed) ++m.deadline_met;
            m.mean_turnaround+=j.finish-j.job.release;
            m.mean_wait+=j.finish-j.job.release-j.job.execution;
            m.max_lateness=std::max(m.max_lateness,static_cast<double>(std::max<Tick>(0,j.finish-j.job.deadline)));
        }
    }
    if (started) m.mean_response/=started;
    if (m.completed) { m.mean_wait/=m.completed; m.mean_turnaround/=m.completed; }
    result.ready_heap=ready.stats(); result.deadline_heap=deadlines.stats();
    return result;
}
}
