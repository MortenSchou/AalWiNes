//
// Created by Dan Kristiansen on 4/9/20.
//

#include <aalwines/model/Network.h>
#include <aalwines/synthesis/FastRerouting.h>
#include <boost/program_options/options_description.hpp>
#include <fstream>
#include <random>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace aalwines;

void write_query(const Router* start_router, const Router* end_router, std::stringstream* s){
    *s << "<.> ";
    *s << "[.#" + start_router->name() + "]";
    *s << " .* ";
    *s << "[" + end_router->name() + "#.]";
    *s << " <.> 0 DUAL\n";
}

void create_path(Network& network, std::function<FastRerouting::label_t(void)>& next_label, uint64_t* i){
    int randomDecision = rand()%3;
    std::vector<std::vector<const Router*>> vector;
    vector.push_back({network.get_router(0), network.get_router(1),
                         network.get_router(3), network.get_router(4),
                         network.get_router(2), network.get_router(3)});
    vector.push_back({network.get_router(0), network.get_router(1),
                         network.get_router(3)});
    vector.push_back({network.get_router(0), network.get_router(2),
                         network.get_router(4), network.get_router(3)});
    FastRerouting::make_data_flow(network.get_router(0)->find_interface("iRouter0"),
                                  network.get_router(3)->find_interface("iRouter3"),
                                  next_label, vector[randomDecision]);
}

void concat_network(Network* synthetic_network, uint64_t* i, std::function<FastRerouting::label_t(void)>& next_label, size_t size){
    *i = *i - 1;
    for(size_t n = 0; n < size; n++, *i = *i - 1){
        Network synthetic_network_concat = Network::construct_synthetic_network();
        create_path(synthetic_network_concat, next_label, i);
        synthetic_network->concat_network(synthetic_network->get_router(n == 0 ? 3 : (3 + 1 + (5 * n)))->find_interface("iRouter3"),
                                         std::move(synthetic_network_concat),
                                         synthetic_network_concat.get_router(0)->find_interface("iRouter0"),
                                         {Query::MPLS, 0, *i});
    }
}

void inject_network(Network* synthetic_network, uint64_t* i, std::function<FastRerouting::label_t(void)>& next_label, size_t size){
    for(size_t n = 1; n <= size; n++) {
        Network synthetic_network_inject = Network::construct_synthetic_network();
        create_path(synthetic_network_inject, next_label, i);
        size_t network_index = n == 1? 0 : n * 5 - 4;
        synthetic_network->inject_network(
                synthetic_network->get_router(0 + network_index)->find_interface("Router2"),
                std::move(synthetic_network_inject),
                synthetic_network_inject.get_router(0)->find_interface("iRouter0"),
                synthetic_network_inject.get_router(3)->find_interface("iRouter3"),
                {Query::MPLS, 0, *i - 5},
                {Query::MPLS, 0, *i});
    }
}

std::pair<Router*,Router*> make_query(Network* network) {
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 eng(rd()); // seed the generator
    std::uniform_int_distribution<> distr(0, network->size() - 1); // define the range
    Router *random_router;
    Router *random_end_router;
    do {
        random_router = network->get_router(distr(eng));
    } while (random_router->is_null() || random_router->get_null_interface() == nullptr);
    do {
        random_end_router = network->get_router(distr(eng));
    } while (random_end_router->is_null() || random_end_router->get_null_interface() == nullptr);
    if(random_router->index() < random_end_router->index()){
        return std::make_pair(random_router, random_end_router);
    }
    return std::make_pair(random_end_router, random_router);
}

std::vector<const Router*> make_flow_concat(Network* network, std::pair<Router*,Router*> router_pair){
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 eng(rd()); // seed the generator
    std::vector<const Router*> path;

    path.emplace_back(router_pair.first);
    Router* end_router = router_pair.second;

    while(path.back() != end_router){
        bool go_nest = false;
        std::string end_router_interface_name = "";
        //TODO: Make pretty, get nesting of router.
        if(floor((path.back()->index()-1) / 5) < floor((end_router->index()-1) / 5)){
            end_router_interface_name = "Router3";
            go_nest = true;
        } else {
            end_router_interface_name = end_router->name();
            end_router_interface_name.erase(std::remove(end_router_interface_name.begin(), end_router_interface_name.end(), '\''), end_router_interface_name.end());
        }
        if(Interface* inf = const_cast<Router*>(path.back())->find_interface(end_router_interface_name)){
            path.emplace_back(inf->target());
            if(go_nest) {
                inf = const_cast<Router*>(path.back())->find_interface("iRouter3");
                path.emplace_back(inf->target());
            }
        } else {
            std::uniform_int_distribution<> distr(1, path.back()->interfaces().size() - 1);
            Router* router;
            do{
                router = path.back()->interfaces()[distr(eng)]->target();
            }
            while(router->is_null());
            path.emplace_back(router);
        }
    }
    return path;
}

int main(int argc, const char** argv) {
    po::options_description opts;
    opts.add_options()
            ("help,h", "produce help message");

    size_t size = 1;
    size_t concat_inject = 0;
    size_t number_networks = 1;
    size_t number_dataflow = 0;
    po::options_description generate("Test Options");
    generate.add_options()
            ("size,s", po::value<size_t>(&size), "size of synthetic network")
            ("mode,m", po::value<size_t>(&concat_inject), "0 = concat and 1 = inject")
            ("networks,n", po::value<size_t>(&number_networks), "amount of test networks")
            ("flow,f", po::value<size_t>(&number_dataflow), "amount of data flow in the network")
            ;
    opts.add(generate);
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, opts), vm);
    po::notify(vm);

    for(size_t n = 0; n < number_networks; n++) {
        std::string name =
                "Test_F" + std::to_string(number_dataflow) + "_M" + std::to_string(concat_inject) + "_D" + std::to_string(size);
        auto synthetic_network = Network::construct_synthetic_network();
        uint64_t i = 42;
        std::function<FastRerouting::label_t(void)> next_label = [&i]() {
            return Query::label_t(Query::type_t::MPLS, 0, i++);
        };
        create_path(synthetic_network, next_label, &i);

        std::stringstream queries;
        if (concat_inject == 0) {
            concat_network(&synthetic_network, &i, next_label, size);
            //Create random dataflow
            for(size_t f = 0; f < number_dataflow; f++){
                auto router_pair = make_query(&synthetic_network);
                auto path = make_flow_concat(&synthetic_network, router_pair);
                FastRerouting::make_data_flow(path[0]->get_null_interface(),path[path.size() - 1]->get_null_interface(), next_label, path);
                write_query(path[0], path[path.size() - 1], &queries);
            }
        } else {
            inject_network(&synthetic_network, &i, next_label, size);
            for(size_t f = 0; f < number_dataflow; f++) {
                auto router_pair = make_query(&synthetic_network);
                write_query(router_pair.first, router_pair.second, &queries);
            }
        }

        //Write query through whole network
        write_query(synthetic_network.get_router(0),
                synthetic_network.get_router(synthetic_network.size()-2), &queries);

        std::ofstream out_topo(name + "-topo.xml");
        if (out_topo.is_open()) {
            synthetic_network.write_prex_topology(out_topo);
        } else {
            std::cerr << "Could not open --write-topology file for writing" << std::endl;
            exit(-1);
        }
        std::ofstream out_route(name + "-routing.xml");
        if (out_route.is_open()) {
            synthetic_network.write_prex_routing(out_route);
        } else {
            std::cerr << "Could not open --write-routing file for writing" << std::endl;
            exit(-1);
        }
        std::ofstream out_query(name + "-queries.q");
        if (out_query.is_open()) {
            out_query << queries.rdbuf();
        } else {
            std::cerr << "Could not open --write-query file for writing" << std::endl;
            exit(-1);
        }
    }
}