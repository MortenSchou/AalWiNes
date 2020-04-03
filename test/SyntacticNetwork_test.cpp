//
// Created by Morten on 18-03-2020.
//

#define BOOST_TEST_MODULE SyntacticNetwork

#include <boost/test/unit_test.hpp>
#include <aalwines/model/Network.h>
#include <aalwines/query/QueryBuilder.h>
#include <pdaaal/Solver_Adapter.h>
#include <fstream>
#include <aalwines/utils/outcome.h>
#include <aalwines/model/NetworkPDAFactory.h>
#include <pdaaal/Reducer.h>
#include <aalwines/synthesis/FastRerouting.h>

using namespace aalwines;

void add_entry(Interface& from_interface, Interface& to_interface, RoutingTable::op_t type, int weight, int interface_label_in, int interface_label_out = 0) {
    RoutingTable table;
    auto& entry = table.push_entry();
    entry._ingoing = &from_interface;           //From interface
    Query::type_t q_type = Query::MPLS;
    entry._top_label.set_value(q_type, interface_label_in, 0);

    entry._rules.emplace_back();
    entry._rules.back()._via = &to_interface;  //Rule to
    entry._rules.back()._type = RoutingTable::MPLS;
    entry._rules.back()._weight = weight;
    entry._rules.back()._ops.emplace_back();
    auto& op = entry._rules.back()._ops.back();
    op._op_label.set_value(q_type, interface_label_out, 0);
    op._op = type;

    std::ostream& warnings = std::cerr;
    table.sort();
    from_interface.table().merge(table, from_interface, warnings);
}

void pair_and_assert(Interface* interface1, Interface* interface2){
    interface1->make_pairing(interface2);
    assert(interface1->match() == interface2);
    assert(interface2->match() == interface1);
}

Network construct_synthetic_network(int nesting = 1){
    int router_size = 5 * nesting;
    std::string router_name = "Router";
    std::vector<std::string> router_names;
    std::vector<std::unique_ptr<Router> > _routers;
    std::vector<const Interface*> _all_interfaces;
    Network::routermap_t _mapping;

    for(int i = 0; i < router_size; i++) {
        router_names.push_back(router_name + std::to_string(i));
    }

    int network_node = 0;
    bool nested = nesting > 1;
    bool fall_through = false;
    int last_nesting_begin = nesting * 5 - 5;

    std::vector<std::vector<std::string>> links;

    for(int i = 0; i < router_size; i++, network_node++) {
        router_name = router_names[i];
        _routers.emplace_back(std::make_unique<Router>(i));
        Router &router = *_routers.back().get();
        router.add_name(router_name);
        router.get_interface(_all_interfaces, "i" + router_names[i]);
        auto res = _mapping.insert(router_name.c_str(), router_name.length());
        _mapping.get_data(res.second) = &router;
        switch (network_node) {
            case 1:
                router.get_interface(_all_interfaces, router_names[i - 1]);
                router.get_interface(_all_interfaces, router_names[i + 2]);
                links.push_back({router_names[i - 1], router_names[i + 2]} );
                break;
            case 2:
                router.get_interface(_all_interfaces, router_names[i + 1]);
                router.get_interface(_all_interfaces, router_names[i + 2]);
                links.push_back({router_names[i + 1], router_names[i + 2]});
                if(nested){
                    router.get_interface(_all_interfaces, router_names[i + 6]);
                    links.back().push_back({router_names[i + 6]});
                } else {
                    router.get_interface(_all_interfaces, router_names[i - 2]);
                    links.back().push_back({router_names[i - 2]});
                }
                break;
            case 3:
                router.get_interface(_all_interfaces, router_names[i - 2]);
                router.get_interface(_all_interfaces, router_names[i + 1]);
                router.get_interface(_all_interfaces, router_names[i - 1]);
                links.push_back({router_names[i - 2], router_names[i + 1], router_names[i - 1]});
                if(i != 3) {
                    router.get_interface(_all_interfaces, router_names[i - 6]);
                    links.back().push_back({router_names[i - 6]});
                }
                break;
            case 4:
                router.get_interface(_all_interfaces, router_names[i - 2]);
                router.get_interface(_all_interfaces, router_names[i - 1]);
                links.push_back({router_names[i - 2], router_names[i - 1]});
                break;
            case 5:
                network_node = 0;   //Fall through
                if(i == last_nesting_begin){
                    nested = false;
                }
                fall_through = true;
            case 0:
                router.get_interface(_all_interfaces, router_names[i + 1]);
                links.push_back({router_names[i + 1]});
                if(nested){
                    router.get_interface(_all_interfaces, router_names[i + 5]);
                    links.back().push_back({router_names[i + 5]});
                } else {
                    router.get_interface(_all_interfaces, router_names[i + 2]);
                    links.back().push_back({router_names[i + 2]});
                }
                if(fall_through){
                    router.get_interface(_all_interfaces, router_names[i - 5]);
                    links.back().push_back({router_names[i - 5]});
                    fall_through = false;
                }
                break;
            default:
                throw base_error("Something went wrong in the construction");
        }
    }
    for (size_t i = 0; i < router_size; ++i) {
        for (const auto &other : links[i]) {
            auto res1 = _mapping.exists(router_names[i].c_str(), router_names[i].length());
            assert(res1.first);
            auto res2 = _mapping.exists(other.c_str(), other.length());
            if(!res2.first) continue;
            _mapping.get_data(res1.second)->find_interface(other)->make_pairing(_mapping.get_data(res2.second)->find_interface(router_names[i]));
        }
    }
    Router::add_null_router(_routers, _all_interfaces, _mapping); //Last router
    return Network(std::move(_mapping), std::move(_routers), std::move(_all_interfaces));
}

BOOST_AUTO_TEST_CASE(NetworkConstructionAndTrace) {
    Network synthetic_network = construct_synthetic_network(1);
    //Network synthetic_network2 = construct_synthetic_network();

    std::vector<const Router*> path {synthetic_network.get_router(0),
                                     synthetic_network.get_router(2),
                                     synthetic_network.get_router(4),
                                     synthetic_network.get_router(3)};

    FastRerouting::make_data_flow(
            synthetic_network.get_router(0)->find_interface("iRouter0"),
            synthetic_network.get_router(3)->find_interface("iRouter3"),
            Query::label_t::any_ip, Query::label_t(Query::type_t::MPLS, 0, 123), path);


    //synthetic_network.manipulate_network( synthetic_network.get_router(0), synthetic_network.get_router(2), synthetic_network2, synthetic_network2.get_router(0), synthetic_network2.get_router(3));

    std::stringstream s_middle;
    synthetic_network.print_simple(s_middle);
    BOOST_TEST_MESSAGE(s_middle.str());

    Builder builder(synthetic_network);
    {
        std::string query("<.*> [.#Router0] .* [Router4#.] <.*> 0 OVER \n"
                          "<.*> [Router0#.] .* [.#Router0prime] <.*> 0 OVER \n"
                          "<.*> [.#Router1] .* [Router2prime#.] <.*> 0 OVER \n"
                          "<.*> [.#Router0] .* [Router1prime#.] <.*> 0 OVER \n"
                          "<.*> [.#Router0prime] .* [Router3prime#.] <.*> 0 OVER \n"
                          "<.*> [Router0prime#.] .* [Router3prime#.] <.*> 0 OVER \n"
                          "<.*> [Router1#.] .* [Router0prime#.] <.*> 0 OVER \n"
                          "<.*> [.#Router3prime] .* [Router2#.] <.*> 0 OVER \n"
        //        "<[6]> [.#Router1] .* [Router3#.] <.*> 0 OVER \n"
        //        "<.*> [Router0#.] .* [.#Router4] <.*> 0 OVER \n"
//                "<.*> [Router0#.] .* [Router2#.] <.*> 0 OVER \n"
//                "<.*> [Router0#.] .* [Router3#.] <.*> 0 OVER \n"
//                "<.*> [.#Router0] .* [Router7#.] <.*> 0 OVER \n"
//                "<.*> [Router1#.] .* [.#Router7] <.*> 0 OVER \n"
//                "<.*> [Router0#.] .* [.#Router7] <.*> 0 OVER \n"
//                "<.*> [.#Router0] .* [Router9#.] <.*> 0 OVER \n"
//                "<.*> [.#Router5] .* [Router7#.] <.*> 0 OVER \n"
//                "<.*> [.#Router8] .* [Router2#.] <.*> 0 OVER \n"
//                "<.*> [.#Router8] .* [Router2#.] <.*> 0 OVER \n"
//                "<.*> [Router4#.] .* [Router2#.] <.*> 0 OVER \n"
        );
        //Adapt to existing query parser
        std::istringstream qstream(query);
        builder.do_parse(qstream);
    }

    size_t query_no = 0;

    pdaaal::Solver_Adapter solver;
    for(auto& q : builder._result) {
        query_no++;
        std::vector<Query::mode_t> modes{q.approximation()};

        bool was_dual = q.approximation() == Query::DUAL;
        if (was_dual)
            modes = std::vector<Query::mode_t>{Query::OVER, Query::UNDER};
        std::pair<size_t, size_t> reduction;
        utils::outcome_t result = utils::MAYBE;
        std::vector<pdaaal::TypedPDA<Query::label_t>::tracestate_t> trace;
        std::stringstream proof;

        size_t tos = 0;
        bool no_ip_swap = false;
        bool get_trace = true;

        for (auto m : modes) {
            q.set_approximation(m);
            NetworkPDAFactory factory(q, synthetic_network, no_ip_swap);
            auto pda = factory.compile();
            reduction = pdaaal::Reducer::reduce(pda, tos, pda.initial(), pda.terminal());
            bool need_trace = was_dual || get_trace;

            auto solver_result1 = solver.post_star<pdaaal::Trace_Type::Any>(pda);
            bool engine_outcome = solver_result1.first;
            if (need_trace && engine_outcome) {
                trace = solver.get_trace(pda, std::move(solver_result1.second));
                if (factory.write_json_trace(proof, trace))
                    result = utils::YES;
            }
            if (q.number_of_failures() == 0)
                result = engine_outcome ? utils::YES : utils::NO;

            if (result == utils::MAYBE && m == Query::OVER && !engine_outcome)
                result = utils::NO;
            if (result != utils::MAYBE)
                break;
        }
        std::cout << "\t\"Q" << query_no << "\" : {\n\t\t\"result\":";
        switch (result) {
            case utils::MAYBE:
                std::cout << "null";
                break;
            case utils::NO:
                std::cout << "false";
                break;
            case utils::YES:
                std::cout << "true";
                break;
        }
        std::cout << ",\n";
        std::cout << "\t\t\"reduction\":[" << reduction.first << ", " << reduction.second << "]";
        if (get_trace && result == utils::YES) {
            std::cout << ",\n\t\t\"trace\":[\n";
            std::cout << proof.str();
            std::cout << "\n\t\t]";
        }
        std::cout << "\n\t}";
        if (query_no != builder._result.size())
            std::cout << ",";
        std::cout << "\n";
        std::cout << "\n}}" << std::endl;
    }
    BOOST_CHECK_EQUAL(true, true);
}

BOOST_AUTO_TEST_CASE(NetworkConstruction) {
    Network synthetic_network = construct_synthetic_network(1);
    Network synthetic_network2 = construct_synthetic_network();
    //Interface()* in_going_interface = synthetic_network.get_router(2);
    //Interface()* in_going_nested_interface = synthetic_network2.get_router(0);
    //synthetic_network.manipulate_network(in_going_interface, 0, 2, synthetic_network2, 0, synthetic_network2.size() - 3);
    //synthetic_network.print_dot(std::cout);

    BOOST_CHECK_EQUAL(true, true);
}