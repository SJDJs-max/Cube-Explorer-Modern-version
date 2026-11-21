#include "cube/model.hpp"
#include "cube/pattern.hpp"
#include "cube/twophase.hpp"
#include "cube/optimal.hpp"
#include "cube/template_search.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThreadPool>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>

namespace {
QString qs(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
std::string ss(const QString& value) { const auto bytes=value.toUtf8(); return {bytes.data(), static_cast<std::size_t>(bytes.size())}; }
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
[[noreturn]] void fail(const QString& text) { throw std::invalid_argument(ss(text)); }

double number(const QJsonObject& o, const char* key, double fallback, double low, double high) {
    if (!o.contains(key)) return fallback;
    const auto value=o.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::floor(value.toDouble())!=value.toDouble() ||
        value.toDouble()<low || value.toDouble()>high) fail(QString("%1 must be an integer in [%2,%3]").arg(key).arg(low).arg(high));
    return value.toDouble();
}
bool boolean(const QJsonObject& o, const char* key, bool fallback) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isBool()) fail(QString("%1 must be boolean").arg(key));
    return o.value(key).toBool();
}
QString string(const QJsonObject& o, const char* key, const QString& fallback={}) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isString()) fail(QString("%1 must be a string").arg(key));
    return o.value(key).toString();
}
template<class T> QJsonArray array_json(const T& a) {
    QJsonArray result; for(auto x:a) result.append(static_cast<int>(x)); return result;
}
template<class T> void read_array(const QJsonObject& o,const char* key,T& a,int low,int high,bool required=true) {
    if(!o.contains(key)) { if(required) fail(QString("Missing %1").arg(key)); return; }
    if(!o.value(key).isArray() || o.value(key).toArray().size()!=static_cast<qsizetype>(a.size())) fail(QString("%1 has incorrect array size").arg(key));
    const auto input=o.value(key).toArray();
    for(qsizetype i=0;i<input.size();++i) {
        if(!input[i].isDouble() || input[i].toDouble()!=input[i].toInt() || input[i].toInt()<low || input[i].toInt()>high)
            fail(QString("%1[%2] is outside its integer range").arg(key).arg(i));
        a[i]=static_cast<typename T::value_type>(input[i].toInt());
    }
}
QJsonObject pattern_json(const cube::PatternState& p) {
    return {{"cp",array_json(p.cp)},{"co",array_json(p.co)},{"ep",array_json(p.ep)},{"eo",array_json(p.eo)},
            {"center_ori",array_json(p.center_ori)},{"frame",p.frame}};
}
QJsonArray actions_json(std::span<const cube::Move> moves) {
    QJsonArray result;
    for(auto m:moves) result.append(QJsonObject{{"symbol",QString(m.symbol)},{"power",m.power},
        {"turn_cost",m.turn_cost()},{"rotation_count",m.rotation_count()}});
    return result;
}
QJsonArray tokens_json(std::span<const cube::Move> moves) {
    QJsonArray result; for(auto m:moves) result.append(qs(cube::format_move(m))); return result;
}
cube::PatternState input_pattern(const QJsonObject& input) {
    const int count=input.contains("scramble")+input.contains("facelets")+input.contains("pattern");
    if(count!=1) fail("Provide exactly one of scramble, facelets, or pattern.");
    cube::PatternState p;
    if(input.contains("scramble")) {
        const auto moves=cube::parse_moves(ss(string(input,"scramble")));
        if(moves.size()>10000) fail("Input formula exceeds 10000 actions.");
        p=cube::pattern_from_cube(cube::apply(cube::CubeState{},moves));
    } else if(input.contains("facelets")) p=cube::parse_pattern_facelets(ss(string(input,"facelets")));
    else {
        if(!input.value("pattern").isObject()) fail("pattern must be an object.");
        const auto o=input.value("pattern").toObject();
        const QStringList fields={"cp","co","ep","eo","center_ori","frame"};
        for(auto it=o.begin();it!=o.end();++it) if(!fields.contains(it.key())) fail("Unknown pattern field: "+it.key());
        read_array(o,"cp",p.cp,-1,7); read_array(o,"co",p.co,-1,2);
        read_array(o,"ep",p.ep,-1,11); read_array(o,"eo",p.eo,-1,1);
        read_array(o,"center_ori",p.center_ori,-1,3,false);
        p.frame=static_cast<std::uint8_t>(number(o,"frame",0,0,23));
    }
    if(input.contains("center_ori")) read_array(input,"center_ori",p.center_ori,-1,3);
    const auto check=cube::validate_pattern(p);
    if(!check) fail(qs(check.message));
    return p;
}
bool complete(const cube::PatternState& p) {
    const auto known=[](const auto& a){return std::all_of(a.begin(),a.end(),[](int x){return x>=0;});};
    return known(p.cp)&&known(p.co)&&known(p.ep)&&known(p.eo);
}
bool centers_specified(const cube::PatternState& p) {
    return std::any_of(p.center_ori.begin(),p.center_ori.end(),[](int x){return x>=0;});
}
cube::CubeState concrete(const cube::PatternState& p) {
    if(!complete(p)) fail("Ordinary two-phase mode requires a complete cube.");
    cube::CubeState c; c.frame=p.frame;
    std::copy(p.cp.begin(),p.cp.end(),c.cubies.cp.begin()); std::copy(p.co.begin(),p.co.end(),c.cubies.co.begin());
    std::copy(p.ep.begin(),p.ep.end(),c.cubies.ep.begin()); std::copy(p.eo.begin(),p.eo.end(),c.cubies.eo.begin());
    const auto check=cube::validate(c); if(!check) fail(qs(check.message));
    return c;
}
std::vector<cube::Move> optimal_word(const QString& text) {
    auto moves=cube::parse_moves(ss(text));
    if(moves.size()>60) fail("Optimal candidate/exclusion exceeds 60 outer-face actions.");
    for(auto move:moves) if(cube::face_names.find(move.symbol)==std::string_view::npos)
        fail("Optimal candidate/exclusion must contain only outer-face actions.");
    return moves;
}
struct Request {
    QString item_id,mode,direction;
    cube::PatternState pattern;
    cube::PatternRequest formula;
    cube::TwoPhaseOptions ordinary;
    cube::OptimalOptions optimal;
    cube::TemplateSearchRequest templates;
    bool find_generators = false;
    cube::TwoPhaseOptions generator_options;
};
Request parse_template_request(const QJsonObject& o) {
    static const QStringList keys={"id","mode","templates","allowed_templates","continuous",
        "max_results","time_limit_ms","node_limit","find_generators","generator_options"};
    for(auto it=o.begin();it!=o.end();++it) if(!keys.contains(it.key()))
        fail("Unknown template search parameter: "+it.key());
    Request r; r.item_id=string(o,"id"); r.mode="template"; r.direction="enumerate";
    if(!o.value("templates").isArray()||o.value("templates").toArray().size()!=5)
        fail("templates must contain exactly five arrays of nine abstract labels (0..5).");
    const auto templates=o.value("templates").toArray();
    for(int t=0;t<5;++t) {
        QJsonObject wrapped{{"labels",templates[t]}};
        read_array(wrapped,"labels",r.templates.templates[t],0,5);
    }
    for(auto& row:r.templates.allowed_templates) row.fill(true);
    if(o.contains("allowed_templates")) {
        if(!o.value("allowed_templates").isArray()||o.value("allowed_templates").toArray().size()!=6)
            fail("allowed_templates must be a 6 by 5 boolean matrix in URFDLB order.");
        const auto matrix=o.value("allowed_templates").toArray();
        for(int face=0;face<6;++face) {
            if(!matrix[face].isArray()||matrix[face].toArray().size()!=5)
                fail("Each allowed_templates face row must contain exactly five booleans.");
            const auto row=matrix[face].toArray();
            for(int t=0;t<5;++t) {
                if(!row[t].isBool()) fail("allowed_templates values must be booleans.");
                r.templates.allowed_templates[face][t]=row[t].toBool();
            }
        }
    }
    r.templates.continuous=boolean(o,"continuous",false);
    r.templates.max_results=static_cast<std::uint64_t>(number(o,"max_results",100,0,1000000));
    r.templates.time_limit=std::chrono::milliseconds(static_cast<std::int64_t>(number(o,"time_limit_ms",30000,0,86400000)));
    r.templates.node_limit=static_cast<std::uint64_t>(number(o,"node_limit",20000000,0,9007199254740991.0));
    r.find_generators=boolean(o,"find_generators",false);
    if(o.contains("generator_options")) {
        if(!r.find_generators) fail("generator_options requires find_generators=true.");
        if(!o.value("generator_options").isObject()) fail("generator_options must be an object.");
        const auto options=o.value("generator_options").toObject();
        const QStringList fields={"max_turns","target_turns","max_phase1_turns","time_limit_ms","node_limit","use_triple_search"};
        for(auto it=options.begin();it!=options.end();++it) if(!fields.contains(it.key()))
            fail("Unknown generator option: "+it.key());
        r.generator_options.max_moves=static_cast<int>(number(options,"max_turns",30,0,60));
        r.generator_options.target_moves=static_cast<int>(number(options,"target_turns",20,0,60));
        r.generator_options.max_phase1_moves=static_cast<int>(number(options,"max_phase1_turns",20,0,30));
        r.generator_options.time_limit=std::chrono::milliseconds(static_cast<std::int64_t>(number(options,"time_limit_ms",5000,0,86400000)));
        r.generator_options.node_limit=static_cast<std::uint64_t>(number(options,"node_limit",0,0,9007199254740991.0));
        r.generator_options.use_triple_search=boolean(options,"use_triple_search",false);
    }
    return r;
}
Request parse_request(const QJsonObject& o) {
    if(string(o,"mode","two_phase")=="template") return parse_template_request(o);
    static const QStringList keys={"id","mode","direction","scramble","facelets","pattern","center_ori","move_symbols","allowed_moves",
        "min_turns","max_turns","target_turns","max_phase1_turns","max_results","time_limit_ms","node_limit","modulo_u",
        "remove_u_symmetric_prefix","reject_alternating_triples","deduplicate_commuting","memoize_dead_ends","quotient_free_rotations","heuristic",
        "initial_solution","excluded_solutions","use_triple_search"};
    for(auto it=o.begin();it!=o.end();++it) if(!keys.contains(it.key())) fail("Unknown search parameter: "+it.key());
    Request r; r.item_id=string(o,"id"); r.mode=string(o,"mode","two_phase"); r.direction=string(o,"direction","solve");
    if(r.mode!="two_phase"&&r.mode!="formula"&&r.mode!="optimal") fail("mode must be two_phase, optimal, formula or template.");
    if(r.direction!="solve"&&r.direction!="generate") fail("direction must be solve or generate.");
    r.pattern=input_pattern(o);
    const auto milliseconds=static_cast<std::int64_t>(number(o,"time_limit_ms",r.mode=="formula"?0:r.mode=="two_phase"?5000:30000,0,86400000));
    const auto nodes=static_cast<std::uint64_t>(number(o,"node_limit",0,0,9007199254740991.0));
    if(r.mode!="two_phase"&&o.contains("use_triple_search")) fail("use_triple_search is only supported in two_phase mode.");
    if(r.mode!="optimal") for(auto key:{"initial_solution","excluded_solutions"})
        if(o.contains(key)) fail(QString("%1 is only supported in optimal mode.").arg(key));
    if(r.mode!="formula") {
        if(!complete(r.pattern)||centers_specified(r.pattern)) fail("Partial or center-direction constraints require formula mode.");
        for(auto key:{"move_symbols","allowed_moves","min_turns","max_results","modulo_u","remove_u_symmetric_prefix","reject_alternating_triples","deduplicate_commuting","memoize_dead_ends","quotient_free_rotations","heuristic"})
            if(o.contains(key)) fail(QString("%1 is a formula-mode parameter; ordinary solving retains its original move set and exact goal.").arg(key));
        const auto input=concrete(r.pattern);
        if(r.mode=="two_phase") {
            r.ordinary.max_moves=static_cast<int>(number(o,"max_turns",30,0,60));
            r.ordinary.target_moves=static_cast<int>(number(o,"target_turns",20,0,60));
            r.ordinary.max_phase1_moves=static_cast<int>(number(o,"max_phase1_turns",20,0,30));
            r.ordinary.time_limit=std::chrono::milliseconds(milliseconds); r.ordinary.node_limit=nodes;
            r.ordinary.use_triple_search=boolean(o,"use_triple_search",false);
        } else {
            for(auto key:{"target_turns","max_phase1_turns"}) if(o.contains(key))
                fail(QString("%1 is only supported in two_phase mode.").arg(key));
            auto& p=r.optimal;
            p.max_moves=static_cast<int>(number(o,"max_turns",30,0,60));
            p.time_limit=std::chrono::milliseconds(milliseconds); p.node_limit=nodes;
            if(o.contains("initial_solution")) {
                auto moves=optimal_word(string(o,"initial_solution"));
                if(r.direction=="generate") moves=cube::inverse(moves);
                const auto terminal=cube::apply(input,moves);
                if(!cube::is_solved(terminal)||!cube::verify_sequence(input,moves,terminal))
                    fail("initial_solution does not exactly solve/generate the input in the requested direction.");
                p.initial_solution=std::move(moves);
            }
            if(o.contains("excluded_solutions")) {
                if(!o.value("excluded_solutions").isArray()||o.value("excluded_solutions").toArray().size()>10000)
                    fail("excluded_solutions must be an array of at most 10000 formula strings.");
                for(auto value:o.value("excluded_solutions").toArray()) {
                    if(!value.isString()) fail("Each excluded solution must be a formula string.");
                    auto moves=optimal_word(value.toString());
                    if(r.direction=="generate") moves=cube::inverse(moves);
                    p.excluded_solutions.push_back(cube::format_moves(moves));
                }
            }
        }
    } else {
        for(auto key:{"target_turns","max_phase1_turns"}) if(o.contains(key)) fail(QString("%1 is only supported in two_phase mode.").arg(key));
        auto& p=r.formula; p.pattern=r.pattern;
        p.direction=r.direction=="solve"?cube::PatternDirection::Solve:cube::PatternDirection::Generate;
        p.min_turns=static_cast<int>(number(o,"min_turns",0,0,2147483647)); p.max_turns=static_cast<int>(number(o,"max_turns",-1,-1,2147483647));
        if(p.max_turns>=0&&p.min_turns>p.max_turns) fail("min_turns exceeds max_turns.");
        p.max_results=static_cast<std::size_t>(number(o,"max_results",0,0,1000000));
        p.collect_solutions=false;
        p.time_limit=std::chrono::milliseconds(milliseconds); p.max_nodes=nodes;
        p.modulo_u=boolean(o,"modulo_u",true); p.remove_u_symmetric_prefix=boolean(o,"remove_u_symmetric_prefix",true);
        p.reject_alternating_triples=boolean(o,"reject_alternating_triples",true); p.deduplicate_commuting=boolean(o,"deduplicate_commuting",true);
        p.memoize_dead_ends=boolean(o,"memoize_dead_ends",true);
        p.quotient_free_rotations=boolean(o,"quotient_free_rotations",true);
        const auto heuristic=string(o,"heuristic","standard");
        if(heuristic=="none")p.heuristic=cube::PatternHeuristic::None;
        else if(heuristic=="standard")p.heuristic=cube::PatternHeuristic::Standard;
        else if(heuristic=="strong")p.heuristic=cube::PatternHeuristic::Strong;
        else fail("heuristic must be none, standard, or strong.");
        if(o.contains("move_symbols")&&o.contains("allowed_moves")) fail("Use move_symbols or allowed_moves, not both.");
        p.allowed_moves.clear();
        if(o.contains("allowed_moves")) {
            if(!o.value("allowed_moves").isArray()) fail("allowed_moves must be an array of individual move strings.");
            for(const auto value:o.value("allowed_moves").toArray()) {
                if(!value.isString()) fail("Each allowed move must be a string.");
                auto parsed=cube::parse_moves(ss(value.toString()));
                if(parsed.size()!=1) fail("Each allowed move must contain exactly one action.");
                p.allowed_moves.push_back(parsed[0]);
            }
        } else {
            for(const auto symbol:ss(string(o,"move_symbols","URFDLB"))) {
                if(cube::move_names.find(symbol)==std::string_view::npos) fail("Unknown move symbol.");
                for(int power=1;power<=3;++power) p.allowed_moves.push_back({symbol,static_cast<std::uint8_t>(power)});
            }
        }
    }
    return r;
}

QJsonObject solution_json(const cube::PatternSolution& s) {
    return {{"formula",qs(cube::format_moves(s.moves,true))},{"moves",tokens_json(s.moves)},{"actions",actions_json(s.moves)},
        {"turns",s.turn_cost},{"rotations",s.rotations},{"metric","HTM+slice+wide; rotations separate"},
        {"residual_u",s.residual_u},{"verified",s.verified},{"optimality_proven",s.optimality_proven},{"terminal",pattern_json(s.terminal)},
        {"terminal_facelets",qs(cube::format_pattern_facelets(s.terminal))}};
}
QJsonObject ordinary_json(const Request& request,const cube::CubeState& input,const cube::TwoPhaseSolution& s) {
    auto moves=s.moves;
    cube::CubeState start=input,terminal=s.final_state;
    if(request.direction=="generate") {
        moves=cube::inverse(moves); start={}; start.frame=input.frame;
        terminal=cube::apply(start,moves);
        if(terminal.cubies!=input.cubies) throw std::logic_error("Exact generator failed its target.");
        if(!cube::verify_sequence(start,moves,terminal)) throw std::logic_error("Exact generator failed physical replay.");
    }
    return {{"formula",qs(cube::format_moves(moves))},{"moves",tokens_json(moves)},{"actions",actions_json(moves)},
        {"turns",s.turn_count},{"rotations",0},{"metric","HTM"},{"residual_u",0},{"verified",true},
        {"solver_phase1_turns",s.phase1_count},{"solver_phase2_turns",s.phase2_count},{"search_axis",s.search_axis},
        {"terminal",pattern_json(cube::pattern_from_cube(terminal,true))},{"terminal_facelets",qs(cube::format_facelets(cube::to_facelets(terminal)))}};
}
QJsonObject optimal_json(const Request& request,const cube::CubeState& input,const cube::OptimalResult& result) {
    const auto& s=result.solution;
    auto moves=s.moves;
    cube::CubeState start=input,terminal=s.final_state;
    if(request.direction=="generate") {
        moves=cube::inverse(moves); start={}; start.frame=input.frame;
        terminal=cube::apply(start,moves);
        if(terminal.cubies!=input.cubies||!cube::verify_sequence(start,moves,terminal))
            throw std::logic_error("Optimal exact generator failed independent replay.");
    }
    return {{"formula",qs(cube::format_moves(moves))},{"moves",tokens_json(moves)},{"actions",actions_json(moves)},
        {"turns",s.turn_count},{"rotations",0},{"metric","HTM"},{"residual_u",0},{"verified",true},
        {"optimality_proven",result.optimality_proven},{"terminal",pattern_json(cube::pattern_from_cube(terminal,true))},
        {"terminal_facelets",qs(cube::format_facelets(cube::to_facelets(terminal)))}};
}

struct Item {
    QString id,status="queued",error,stop_reason;
    QJsonObject specification,progress,summary;
    std::unique_ptr<Request> request;
    std::stop_source cancellation;
    std::size_t result_count=0;
    std::uint64_t depth_start_count=0;
};
struct Job {
    QString id,created=now();
    std::vector<std::shared_ptr<Item>> items;
    std::vector<qint64> result_offsets;
    std::size_t next=0,active=0;
    int parallelism=1;
    bool cancelled=false;
    std::unique_ptr<QFile> result_file;
};

class Engine {
    QThreadPool pool_;
    std::mutex mutex_;
    std::timed_mutex solver_mutex_;
    std::map<QString,std::shared_ptr<Job>> jobs_;
    std::shared_ptr<cube::TwoPhaseSolver> solver_;
    QString workspace_,tables_;
    std::atomic<bool> closing_=false;
public:
    Engine(QString workspace,QString tables):workspace_(QDir(workspace).absolutePath()),tables_(std::move(tables)) {
        if(!QDir().mkpath(workspace_+"/jobs")) fail("Cannot create workspace jobs directory.");
        pool_.setMaxThreadCount(std::clamp(static_cast<int>(std::thread::hardware_concurrency()),2,16));
    }
    ~Engine(){shutdown();}
    static bool terminal(const QString& state) { return state=="completed"||state=="cancelled"||state=="failed"||state=="invalid"; }
    QJsonObject status_locked(const std::shared_ptr<Job>& job) {
        QJsonArray items; int done=0;
        for(const auto& item:job->items) {
            done+=terminal(item->status);
            items.append(QJsonObject{{"item_id",item->id},{"state",item->status},{"stop_reason",item->stop_reason},
                {"error",item->error},{"results",static_cast<qint64>(item->result_count)},{"progress",item->progress},{"summary",item->summary}});
        }
        return {{"job_id",job->id},{"created",job->created},{"done",done==static_cast<int>(job->items.size())},
            {"state",done==static_cast<int>(job->items.size())?"finished":job->cancelled?"cancelling":"running"},
            {"completed_items",done},{"total_items",static_cast<qint64>(job->items.size())},
            {"result_count",static_cast<qint64>(job->result_offsets.size())},{"items",items}};
    }
    void persist_locked(const std::shared_ptr<Job>& job) {
        QFile file(workspace_+"/jobs/"+job->id+".json");
        if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)) throw std::runtime_error("Cannot persist job status.");
        auto o=status_locked(job); QJsonArray specifications;
        for(const auto& i:job->items) specifications.append(i->specification);
        o.insert("requests",specifications);
        if(file.write(QJsonDocument(o).toJson(QJsonDocument::Indented))<0) throw std::runtime_error("Job status write failed.");
    }
    void publish(const std::shared_ptr<Job>& job,const std::shared_ptr<Item>& item,QJsonObject solution) {
        std::lock_guard lock(mutex_);
        QJsonObject row{{"sequence",static_cast<qint64>(job->result_offsets.size())},{"item_id",item->id},{"solution",solution}};
        const auto bytes=QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n';
        const auto offset=job->result_file->pos();
        if(job->result_file->write(bytes)!=bytes.size()||!job->result_file->flush()) throw std::runtime_error("Result persistence failed.");
        job->result_offsets.push_back(offset); ++item->result_count;
    }
    void launch_locked(const std::shared_ptr<Job>& job) {
        while(job->active<static_cast<std::size_t>(job->parallelism)&&job->next<job->items.size()) {
            auto item=job->items[job->next++];
            if(item->status!="queued") continue;
            if(job->cancelled||closing_) {item->status="cancelled";item->stop_reason="cancelled";continue;}
            ++job->active; item->status="preparing";
            pool_.start([this,job,item]{run(job,item);});
        }
    }
    std::shared_ptr<cube::TwoPhaseSolver> solver_for(const std::shared_ptr<Item>& item) {
        std::unique_lock solver_lock(solver_mutex_,std::defer_lock);
        while(!solver_lock.try_lock_for(std::chrono::milliseconds(10)))
            if(item->cancellation.stop_requested()) throw std::runtime_error("Cancelled while waiting for solver tables.");
        if(item->cancellation.stop_requested()) throw std::runtime_error("Cancelled before solver preparation.");
        if(!solver_) solver_=std::make_shared<cube::TwoPhaseSolver>(std::filesystem::path(tables_.toStdWString()),
            [this,item](std::string_view text){std::lock_guard lock(mutex_);item->progress={{"message",qs(text)}};},
            item->cancellation.get_token());
        return solver_;
    }
    void run_templates(const std::shared_ptr<Job>& job,const std::shared_ptr<Item>& item) {
        const auto& r=*item->request;
        const auto solver=r.find_generators?solver_for(item):std::shared_ptr<cube::TwoPhaseSolver>{};
        const auto started=std::chrono::steady_clock::now();
        std::uint64_t generator_nodes=0,generated=0;
        std::chrono::milliseconds generator_time{0};
        const auto report=cube::enumerate_templates(r.templates,
            [this,job,item,&r,&solver,started,&generator_nodes,&generated,&generator_time](const cube::CubeState& state) {
                const auto checked=cube::verify_template_result(r.templates,state);
                if(!checked) throw std::logic_error("Template result failed independent verification: "+checked.message);
                QJsonObject output{{"kind","cube_state"},{"pattern",pattern_json(cube::pattern_from_cube(state))},
                    {"facelets",qs(cube::format_facelets(cube::to_facelets(state)))},{"verified",true},
                    {"continuous",r.templates.continuous}};
                if(solver) {
                    auto options=r.generator_options;
                    auto remaining=r.templates.time_limit;
                    bool out_of_time=false;
                    if(remaining.count()>0) {
                        remaining-=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started);
                        out_of_time=remaining.count()<=0;
                        if(!out_of_time&&(options.time_limit.count()==0||remaining<options.time_limit)) options.time_limit=remaining;
                    }
                    cube::TwoPhaseResult result;
                    if(out_of_time) result.stop_reason=cube::TwoPhaseStop::time_limit;
                    else {
                        {
                            std::lock_guard lock(mutex_);
                            item->status=item->cancellation.stop_requested()?"cancelling":"running";
                            item->progress.insert("phase","generator");
                        }
                        result=solver->solve(state,options,item->cancellation.get_token());
                    }
                    generator_nodes+=result.nodes; generator_time+=result.elapsed;
                    output.insert("generator_search",QJsonObject{{"solved",result.solved},
                        {"stop_reason",qs(cube::to_string(result.stop_reason))},{"nodes",static_cast<double>(result.nodes)},
                        {"elapsed_ms",static_cast<qint64>(result.elapsed.count())},{"search_axes",result.search_axes},{"message",qs(result.message)}});
                    if(result.solved) {
                        Request direction; direction.direction="generate";
                        auto generator=ordinary_json(direction,state,result.solution);
                        generator.insert("optimality_proven",false);
                        output.insert("generator",generator); ++generated;
                    }
                }
                publish(job,item,output);
            },
            [this,item](const cube::TemplateSearchProgress& p) {
                std::lock_guard lock(mutex_);
                item->status=item->cancellation.stop_requested()?"cancelling":"running";
                item->progress={{"phase","templates"},{"nodes",static_cast<double>(p.nodes)},
                    {"matched_states",static_cast<double>(p.results)},{"assigned_corners",p.assigned_corners},
                    {"assigned_edges",p.assigned_edges},{"elapsed_ms",static_cast<qint64>(p.elapsed.count())}};
            },item->cancellation.get_token());
        std::lock_guard lock(mutex_);
        item->stop_reason=qs(cube::template_search_stop_name(report.stop));
        item->status=report.stop==cube::TemplateSearchStop::Cancelled?"cancelled":
                     report.stop==cube::TemplateSearchStop::InvalidInput?"invalid":"completed";
        item->error=qs(report.message);
        item->summary={{"result_kind","cube_state"},{"matched_states",static_cast<double>(report.results)},
            {"nodes",static_cast<double>(report.nodes)},{"elapsed_ms",static_cast<qint64>(report.elapsed.count())},
            {"search_complete",report.exhausted()},{"find_generators",r.find_generators},
            {"generated_states",static_cast<double>(generated)},{"generator_nodes",static_cast<double>(generator_nodes)},
            {"generator_elapsed_ms",static_cast<qint64>(generator_time.count())}};
    }
    void run(const std::shared_ptr<Job>& job,const std::shared_ptr<Item>& item) {
        try {
            auto& r=*item->request;
            if(r.mode=="template") run_templates(job,item);
            else if(r.mode=="formula") {
                auto request=r.formula; request.cancelled=[item]{return item->cancellation.stop_requested();};
                auto report=cube::enumerate_pattern(request,
                    [this,job,item](const auto& solution){publish(job,item,solution_json(solution));},
                    [this,item](const cube::PatternProgress& p){
                        std::lock_guard lock(mutex_);
                        item->status=item->cancellation.stop_requested()?"cancelling":p.preparing?"preparing":"running";
                        if(p.depth_started) ++item->depth_start_count;
                        item->progress={{"nodes",static_cast<double>(p.nodes)},{"depth",p.depth},{"results",static_cast<qint64>(p.results)},
                            {"depth_started",p.depth_started},{"depth_start_count",static_cast<double>(item->depth_start_count)},
                            {"elapsed_ms",static_cast<qint64>(p.elapsed.count())}};
                    });
                std::lock_guard lock(mutex_);
                item->stop_reason=qs(cube::pattern_stop_name(report.stop));
                item->status=report.stop==cube::PatternStop::Cancelled?"cancelled":report.stop==cube::PatternStop::InvalidInput?"invalid":"completed";
                item->error=qs(report.message);
                item->summary={{"nodes",static_cast<double>(report.nodes)},{"elapsed_ms",static_cast<qint64>(report.elapsed.count())},
                    {"preparation_ms",static_cast<qint64>(report.preparation_time.count())},{"completed_depth",report.completed_depth},
                    {"search_complete",report.exhausted()},{"root_u_symmetric",report.root_u_symmetric},{"free_rotation_quotient",report.free_rotation_quotient},
                    {"pruning_entries",static_cast<qint64>(report.pruning_entries)},{"results",static_cast<qint64>(report.results)},
                    {"depth_start_count",static_cast<double>(item->depth_start_count)}};
            } else {
                const auto solver=solver_for(item);
                {std::lock_guard lock(mutex_);item->status=item->cancellation.stop_requested()?"cancelling":"running";}
                const auto input=concrete(r.pattern);
                if(r.mode=="optimal") {
                    cube::OptimalSolver optimal(*solver);
                    const auto result=optimal.solve(input,r.optimal,item->cancellation.get_token(),
                        [this,item](const cube::OptimalProgress& p){
                            std::lock_guard lock(mutex_);
                            item->status=item->cancellation.stop_requested()?"cancelling":"running";
                            item->progress={{"nodes",static_cast<double>(p.nodes)},{"depth",p.depth},
                                {"completed_depth",p.completed_depth},{"elapsed_ms",static_cast<qint64>(p.elapsed.count())}};
                        });
                    if(result.solved) publish(job,item,optimal_json(r,input,result));
                    std::lock_guard lock(mutex_);
                    item->stop_reason=qs(cube::to_string(result.stop_reason));
                    item->status=result.stop_reason==cube::OptimalStop::cancelled?"cancelled":result.stop_reason==cube::OptimalStop::invalid_input?"invalid":"completed";
                    if(result.stop_reason==cube::OptimalStop::invalid_input)item->error=qs(result.message);
                    item->summary={{"solved",result.solved},{"nodes",static_cast<double>(result.nodes)},
                        {"elapsed_ms",static_cast<qint64>(result.elapsed.count())},{"optimality_proven",result.optimality_proven},
                        {"completed_depth",result.completed_depth},{"lower_bound",result.lower_bound},
                        {"search_complete",result.stop_reason==cube::OptimalStop::depth_limit},{"message",qs(result.message)}};
                } else {
                auto result=solver->solve(input,r.ordinary,item->cancellation.get_token(),
                    [this,job,item,&r,&input](const auto& solution){publish(job,item,ordinary_json(r,input,solution));});
                std::lock_guard lock(mutex_);
                item->stop_reason=qs(cube::to_string(result.stop_reason));
                item->status=result.stop_reason==cube::TwoPhaseStop::cancelled?"cancelled":result.stop_reason==cube::TwoPhaseStop::invalid_input?"invalid":"completed";
                item->summary={{"solved",result.solved},{"nodes",static_cast<double>(result.nodes)},
                    {"elapsed_ms",static_cast<qint64>(result.elapsed.count())},{"optimality_proven",false},
                    {"search_complete",false},{"search_axes",result.search_axes},{"message",qs(result.message)}};
                }
            }
        } catch(const std::exception& e) {
            std::lock_guard lock(mutex_);
            item->status=item->cancellation.stop_requested()?"cancelled":"failed";
            item->stop_reason=item->status; item->error=QString::fromUtf8(e.what());
        }
        std::lock_guard lock(mutex_);
        --job->active; launch_locked(job);
        try {persist_locked(job);} catch(const std::exception& e){std::cerr<<"Job persistence: "<<e.what()<<'\n';}
    }
    std::shared_ptr<Job> find_locked(const QString& id) {
        const auto it=jobs_.find(id); if(it==jobs_.end()) fail("Unknown job_id in this engine session.");
        return it->second;
    }
    QJsonObject capabilities() {
        return {{"name","Cube Explorer Modern"},{"version","0.3.1"},{"transport","json-lines"},{"protocol_version",1},
            {"modes",QJsonArray{"two_phase","optimal","formula","template"}},{"move_symbols",qs(cube::move_names)},
            {"formula_rotation_cost",0},{"formula_turn_cost",1},{"logical_u_anchor","input centers"},
            {"input_formats",QJsonArray{"scramble","URFDLB facelets (? unknown, lowercase identity-only, @ orientation-only)","pattern arrays"}},
            {"operations",QJsonArray{"capabilities","validate","apply","submit","status","results","cancel","shutdown"}},
            {"parameters",QJsonArray{"mode","direction","scramble","facelets","pattern","center_ori","move_symbols","allowed_moves","min_turns",
                "max_turns","target_turns","max_phase1_turns","max_results","time_limit_ms","node_limit","modulo_u",
                "remove_u_symmetric_prefix","reject_alternating_triples","deduplicate_commuting","memoize_dead_ends","quotient_free_rotations","heuristic",
                "initial_solution","excluded_solutions","use_triple_search","templates","allowed_templates","continuous","find_generators","generator_options"}},
            {"formula_max_turns",2147483647},{"formula_unlimited_depth",-1},
            {"formula_defaults",QJsonObject{{"min_turns",0},{"max_turns",-1},{"max_results",0},{"time_limit_ms",0},{"node_limit",0}}},
            {"maximum_parallelism",16},{"workspace",workspace_},
            {"ordinary_limits","Complete ordinary cubes, outer-face HTM, exact restoration; special constraints require formula mode."},
            {"optimal_limits","Three-axis exact IDA*, 18 outer-face HTM actions. Read optimality_proven independently of solved. Initial/excluded formulas use the requested solve/generate direction."},
            {"template_search",QJsonObject{
                {"result_kind","cube_state"},{"templates_shape",QJsonArray{5,9}},{"abstract_labels",QJsonArray{0,1,2,3,4,5}},
                {"gray_label",5},{"allowed_templates_shape",QJsonArray{6,5}},{"face_order","URFDLB"},
                {"face_variants","eight planar rotations/reflections"},{"global_isomorphism_filter",false},
                {"rules","Labels describe equality classes, not fixed colors. Entirely gray templates are unused; mixed gray is an ordinary sixth class. Each face must match an allowed template."},
                {"defaults",QJsonObject{{"continuous",false},{"max_results",100},{"time_limit_ms",30000},{"node_limit",20000000},{"find_generators",false}}},
                {"generator_defaults",QJsonObject{{"max_turns",30},{"target_turns",20},{"max_phase1_turns",20},{"time_limit_ms",5000},{"node_limit",0},{"use_triple_search",false}}},
                {"budgets","Template time includes enumeration and generator searches, excluding initial shared-table loading. Template node limit counts enumeration nodes; generator_options budgets apply separately per state. Zero limits are unlimited."}
            }}};
    }
    QJsonObject dispatch(const QJsonObject& command) {
        const auto op=string(command,"op");
        if(op=="capabilities") return capabilities();
        if(op=="validate") {
            if(!command.value("request").isObject()) fail("request must be an object.");
            const auto r=parse_request(command.value("request").toObject());
            if(r.mode=="template") {
                QJsonArray templates,matrix;
                for(const auto& t:r.templates.templates) templates.append(array_json(t));
                for(const auto& face:r.templates.allowed_templates) {
                    QJsonArray row; for(bool allowed:face) row.append(allowed); matrix.append(row);
                }
                return {{"valid",true},{"mode","template"},{"result_kind","cube_state"},{"templates",templates},
                    {"allowed_templates",matrix},{"continuous",r.templates.continuous},{"find_generators",r.find_generators},
                    {"max_results",static_cast<double>(r.templates.max_results)},{"node_limit",static_cast<double>(r.templates.node_limit)},
                    {"time_limit_ms",static_cast<qint64>(r.templates.time_limit.count())},{"search_started",false}};
            }
            return {{"valid",true},{"mode",r.mode},{"direction",r.direction},{"complete",complete(r.pattern)},
                {"center_constraints",centers_specified(r.pattern)},{"pattern",pattern_json(r.pattern)},
                {"facelets",qs(cube::format_pattern_facelets(r.pattern))}};
        }
        if(op=="apply") {
            if(!command.value("request").isObject()) fail("request must contain the input cube.");
            auto p=input_pattern(command.value("request").toObject());
            const auto moves=cube::parse_moves(ss(string(command,"moves")));
            if(moves.size()>10000) fail("Formula exceeds 10000 actions.");
            p=cube::apply_pattern(p,moves);
            return {{"pattern",pattern_json(p)},{"facelets",qs(cube::format_pattern_facelets(p))},
                {"moves",tokens_json(moves)},{"turns",cube::turn_count(moves)},{"rotations",cube::rotation_count(moves)}};
        }
        if(op=="submit") {
            if(closing_) fail("Engine is shutting down.");
            if(!command.value("items").isArray()) fail("items must be an array of search requests.");
            const auto inputs=command.value("items").toArray();
            if(inputs.empty()||inputs.size()>10000) fail("A batch must have 1..10000 items.");
            auto job=std::make_shared<Job>(); job->id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            job->parallelism=static_cast<int>(number(command,"parallelism",std::min(4,pool_.maxThreadCount()),1,16));
            QJsonArray ids; std::map<QString,bool> seen;
            for(qsizetype n=0;n<inputs.size();++n) {
                auto item=std::make_shared<Item>();
                item->specification=inputs[n].toObject();
                item->id=item->specification.value("id").isString()?item->specification.value("id").toString():QString::number(n);
                if(item->id.isEmpty())item->id=QString::number(n);
                if(seen.contains(item->id))fail("Duplicate item id in batch: "+item->id);
                seen[item->id]=true; ids.append(item->id);
                try {
                    if(!inputs[n].isObject())fail("Batch item must be an object.");
                    item->request=std::make_unique<Request>(parse_request(item->specification));
                } catch(const std::exception& e){item->status="invalid";item->stop_reason="invalid_input";item->error=QString::fromUtf8(e.what());}
                job->items.push_back(item);
            }
            job->result_file=std::make_unique<QFile>(workspace_+"/jobs/"+job->id+".jsonl");
            if(!job->result_file->open(QIODevice::WriteOnly|QIODevice::Append))fail("Cannot create job results file.");
            std::lock_guard lock(mutex_); jobs_[job->id]=job;
            persist_locked(job);launch_locked(job);
            return {{"job_id",job->id},{"item_ids",ids},{"parallelism",job->parallelism},{"accepted",true}};
        }
        if(op=="status") {
            std::lock_guard lock(mutex_); return status_locked(find_locked(string(command,"job_id")));
        }
        if(op=="results") {
            const auto cursor=static_cast<qsizetype>(number(command,"cursor",0,0,9007199254740991.0));
            const auto limit=static_cast<qsizetype>(number(command,"limit",100,1,1000));
            std::lock_guard lock(mutex_);const auto job=find_locked(string(command,"job_id"));
            const auto available=static_cast<qsizetype>(job->result_offsets.size());
            if(cursor>available)fail("cursor exceeds available results.");
            QJsonArray page; const auto end=std::min(available,cursor+limit);
            QFile reader(job->result_file->fileName());
            if(!reader.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read persisted results.");
            for(qsizetype i=cursor;i<end;++i) {
                if(!reader.seek(job->result_offsets[static_cast<std::size_t>(i)])) throw std::runtime_error("Cannot seek persisted result.");
                QJsonParseError error;
                const auto record=QJsonDocument::fromJson(reader.readLine(),&error);
                if(error.error!=QJsonParseError::NoError||!record.isObject()) throw std::runtime_error("Persisted result is incomplete or corrupted.");
                page.append(record.object());
            }
            return {{"job_id",job->id},{"results",page},{"next_cursor",end},{"available",available},
                {"done",status_locked(job).value("done")},{"has_more",end<available},
                {"result_file",job->result_file->fileName()}};
        }
        if(op=="cancel") {
            std::lock_guard lock(mutex_);const auto job=find_locked(string(command,"job_id"));
            const auto wanted=string(command,"item_id"); bool found=wanted.isEmpty();
            if(wanted.isEmpty())job->cancelled=true;
            for(const auto& item:job->items)if(wanted.isEmpty()||item->id==wanted) {
                found=true;item->cancellation.request_stop();
                if(item->status=="queued"){item->status="cancelled";item->stop_reason="cancelled";}
                else if(!terminal(item->status))item->status="cancelling";
            }
            if(!found)fail("Unknown item_id.");
            persist_locked(job);
            return {{"job_id",job->id},{"cancel_requested",true},{"done",status_locked(job).value("done")}};
        }
        if(op=="shutdown") {shutdown();return {{"stopped",true}};}
        fail("Unknown op: "+op);
    }
    void shutdown() {
        if(closing_.exchange(true))return;
        {
            std::lock_guard lock(mutex_);
            for(const auto& [id,job]:jobs_) {
                (void)id;job->cancelled=true;
                for(const auto& item:job->items) {
                    item->cancellation.request_stop();
                    if(item->status=="queued"){item->status="cancelled";item->stop_reason="cancelled";}
                }
            }
        }
        pool_.waitForDone();
        std::lock_guard lock(mutex_);
        for(const auto& [id,job]:jobs_) { (void)id;try{persist_locked(job);}catch(...){} }
    }
};
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    QCoreApplication::setApplicationName("CubeExplorerEngine");
    QString workspace=QCoreApplication::applicationDirPath()+"/data";
    QString tables=QCoreApplication::applicationDirPath()+"/tables/original";
    if(!QDir(tables).exists())tables=QString::fromUtf8(CUBE_PROJECT_ROOT)+"/tables/original";
    const auto arguments=application.arguments();
    for(int i=1;i<arguments.size();++i) {
        if(arguments[i]=="--workspace"&&i+1<arguments.size())workspace=arguments[++i];
        else if(arguments[i]=="--tables"&&i+1<arguments.size())tables=arguments[++i];
        else if(arguments[i]=="--help") {std::cout<<"CubeExplorerEngine [--workspace DIRECTORY] [--tables DIRECTORY]\nUTF-8 JSON lines on stdin/stdout. Logs on stderr.\n";return 0;}
        else {std::cerr<<"Unknown or incomplete command-line argument.\n";return 2;}
    }
    try {
        Engine engine(workspace,tables);
        std::string line;
        while(std::getline(std::cin,line)) {
            QJsonValue id;QJsonObject response;bool stop=false;
            try {
                if(line.size()>8*1024*1024)fail("Request exceeds 8 MiB.");
                QJsonParseError error;
                auto document=QJsonDocument::fromJson(QByteArray::fromStdString(line),&error);
                if(error.error!=QJsonParseError::NoError||!document.isObject())fail("Request must be a JSON object.");
                const auto command=document.object();id=command.value("id");
                response={{"id",id},{"ok",true},{"result",engine.dispatch(command)}};
                stop=command.value("op").toString()=="shutdown";
            } catch(const std::exception& e) {
                response={{"id",id},{"ok",false},{"error",QJsonObject{{"code","invalid_request"},{"message",QString::fromUtf8(e.what())}}}};
            }
            std::cout<<QJsonDocument(response).toJson(QJsonDocument::Compact).constData()<<std::endl;
            if(stop)break;
        }
        engine.shutdown();
    } catch(const std::exception& e){std::cerr<<"Engine startup failed: "<<e.what()<<'\n';return 1;}
    return 0;
}
