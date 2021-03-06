/* 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 *  Copyright Morten K. Schou
 */

/* 
 * File:   Verifier.h
 * Author: Morten K. Schou <morten@h-schou.dk>
 *
 * Created on 13-08-2020.
 */

#ifndef AALWINES_VERIFIER_H
#define AALWINES_VERIFIER_H

#include <aalwines/utils/json_stream.h>
#include <aalwines/utils/stopwatch.h>
#include <aalwines/utils/outcome.h>
#include <aalwines/query/QueryBuilder.h>
#include <aalwines/model/NetworkPDAFactory.h>
#include <aalwines/model/NetworkWeight.h>
#include <pdaaal/SolverAdapter.h>
#include <pdaaal/Reducer.h>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

namespace aalwines {

    inline void to_json(json & j, const Query::mode_t& mode) {
        static const char *modeTypes[] {"OVER", "UNDER", "DUAL", "EXACT"};
        j = modeTypes[mode];
    }

    using namespace pdaaal;

    class Verifier {
    public:

        explicit Verifier(const std::string& caption = "Verification Options") : verification(caption) {
            verification.add_options()
                    ("engine,e", po::value<size_t>(&_engine), "0=no verification,1=post*,2=pre*")
                    ("tos-reduction,r", po::value<size_t>(&_reduction), "0=none,1=simple,2=dual-stack,3=simple+backup,4=dual-stack+backup")
                    ("trace,t", po::bool_switch(&_print_trace), "Get a trace when possible")
                    ;
        }

        [[nodiscard]] const po::options_description& options() const { return verification; }
        auto add_options() { return verification.add_options(); }

        void check_settings() const {
            if(_reduction > 4) {
                std::cerr << "Unknown value for --tos-reduction : " << _reduction << std::endl;
                exit(-1);
            }
            if(_engine > 2) {
                std::cerr << "Unknown value for --engine : " << _engine << std::endl;
                exit(-1);
            }
        }
        void check_supports_weight() const {
            if (_engine != 1) {
                std::cerr << "Shortest trace using weights is only implemented for --engine 1 (post*). Not for --engine " << _engine << std::endl;
                exit(-1);
            }
        }
        void set_print_trace() { _print_trace = true; }

        template<typename W_FN = std::function<void(void)>>
        void run(Builder& builder, const std::vector<std::string>& query_strings, json_stream& json_output, bool print_timing = true, const W_FN& weight_fn = [](){}) {
            size_t query_no = 0;
            for (auto& q : builder._result) {
                std::stringstream qn;
                qn << "Q" << query_no+1;

                auto res = run_once(builder, q, print_timing, weight_fn);
                res["query"] = query_strings[query_no];
                json_output.entry_object(qn.str(), res);

                ++query_no;
            }
        }

        template<typename W_FN = std::function<void(void)>>
        json run_once(Builder& builder, Query& q, bool print_timing = true, const W_FN& weight_fn = [](){}){
            constexpr static bool is_weighted = pdaaal::is_weighted<typename W_FN::result_type>;

            json output; // Store output information in this JSON object.
            static const char *engineTypes[] {"", "Post*", "Pre*"};
            output["engine"] = engineTypes[_engine];

            // DUAL mode means first do OVER-approximation, then if that is inconclusive, do UNDER-approximation
            std::vector<Query::mode_t> modes = q.approximation() == Query::DUAL ? std::vector<Query::mode_t>{Query::OVER, Query::UNDER} : std::vector<Query::mode_t>{q.approximation()};
            output["mode"] = q.approximation();

            std::stringstream proof;
            std::vector<unsigned int> trace_weight;
            stopwatch compilation_time(false);
            stopwatch reduction_time(false);
            stopwatch verification_time(false);

            utils::outcome_t result = utils::outcome_t::MAYBE;
            for (auto m : modes) {
                proof = std::stringstream(); // Clear stream from previous mode.

                // Construct PDA
                compilation_time.start();
                q.set_approximation(m);
                NetworkPDAFactory factory(q, builder._network, builder.all_labels(), weight_fn);
                auto pda = factory.compile();
                compilation_time.stop();

                // Reduce PDA
                reduction_time.start();
                output["reduction"] = Reducer::reduce(pda, _reduction, pda.initial(), pda.terminal());
                reduction_time.stop();

                // Choose engine, run verification, and (if relevant) get the trace.
                verification_time.start();
                bool engine_outcome;
                switch(_engine) {
                    case 1: {
                        using W = typename W_FN::result_type;
                        SolverAdapter::res_type<W,std::less<W>,pdaaal::add<W>> solver_result;
                        if constexpr (is_weighted) {
                            solver_result = solver.post_star<pdaaal::Trace_Type::Shortest>(pda);
                        } else {
                            solver_result = solver.post_star<pdaaal::Trace_Type::Any>(pda);
                        }
                        engine_outcome = solver_result.first;
                        verification_time.stop();
                        if (engine_outcome) {
                            std::vector<pdaaal::TypedPDA<Query::label_t>::tracestate_t > trace;
                            if constexpr (is_weighted) {
                                std::tie(trace, trace_weight) = solver.get_trace<pdaaal::Trace_Type::Shortest>(pda, std::move(solver_result.second));
                            } else {
                                trace = solver.get_trace<pdaaal::Trace_Type::Any>(pda, std::move(solver_result.second));
                            }
                            if (factory.write_json_trace(proof, trace))
                                result = utils::outcome_t::YES;
                        }
                        break;
                    }
                    case 2: {
                        auto solver_result = solver.pre_star(pda, true);
                        engine_outcome = solver_result.first;
                        verification_time.stop();
                        if (engine_outcome) {
                            auto trace = solver.get_trace(pda, std::move(solver_result.second));
                            if (factory.write_json_trace(proof, trace))
                                result = utils::outcome_t::YES;
                        }
                        break;
                    }
                    default:
                        throw base_error("Unsupported --engine value given");
                }

                // Determine result from the outcome of verification and the mode (over/under-approximation) used.
                if (q.number_of_failures() == 0) {
                    result = engine_outcome ? utils::outcome_t::YES : utils::outcome_t::NO;
                }
                if (result == utils::outcome_t::MAYBE && m == Query::OVER && !engine_outcome) {
                    result = utils::outcome_t::NO;
                }
                if (result != utils::outcome_t::MAYBE) {
                    output["mode"] = m;
                    break;
                }
            }

            output["result"] = result;

            if (_print_trace && result == utils::outcome_t::YES) {
                if constexpr (is_weighted) {
                    output["trace-weight"] = trace_weight;
                }
                std::stringstream trace;
                trace << "[" << proof.str() << "]"; // TODO: Make NetworkPDAFactory::write_json_trace return a json object instead of ad-hoc formatting to a stringstream.
                output["trace"] = json::parse(trace.str());
            }
            if (print_timing) {
                output["compilation-time"] = compilation_time.duration();
                output["reduction-time"] = reduction_time.duration();
                output["verification-time"] = verification_time.duration();
            }

            return output;
        }

    private:
        po::options_description verification;

        // Settings
        size_t _engine = 1;
        size_t _reduction = 0;
        bool _print_trace = false;

        // Solver engines
        SolverAdapter solver;
    };

}

#endif //AALWINES_VERIFIER_H
