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
 *  Copyright Peter G. Jensen, all rights reserved.
 */

/* 
 * File:   NetworkPDAFactory.h
 * Author: Peter G. Jensen <root@petergjoel.dk>
 *
 * Created on July 19, 2019, 6:26 PM
 */

#ifndef NETWORKPDA_H
#define NETWORKPDA_H

#include "Query.h"
#include "Network.h"
#include "pdaaal/model/PDAFactory.h"


namespace mpls2pda
{
    class NetworkPDAFactory : public pdaaal::PDAFactory<Query::label_t> {
        using label_t = Query::label_t;
        using NFA = pdaaal::NFA<label_t>;
        using PDA = pdaaal::PDAFactory<label_t>;
    private:        
        struct nstate_t {
            int32_t _appmode = 0; // mode of approximation
            int32_t _opid = -1; // which operation is the first in the rule (-1=cleaned up).
            int32_t _tid = 0;
            int32_t _eid = 0;
            int32_t _rid = 0;
            NFA::state_t* _nfastate;
            const Router* _router;
        };
    public:
        NetworkPDAFactory(Query& q, Network& network);
    protected:
        const std::vector<size_t>& initial() override;
        bool empty_accept() const override;
        bool accepting(size_t) override;
        std::vector<rule_t> rules(size_t ) override;

    private:
        void construct_initial();
        std::pair<bool,size_t> add_state(NFA::state_t* state, const Router* router, int32_t mode = 0, int32_t tid = 0, int32_t eid = 0, int32_t fid = 0, int32_t op = -1);
        int32_t set_approximation(const nstate_t& state, const RoutingTable::forward_t& forward);
        Network& _network;
        Query& _query;
        NFA& _path;
        std::vector<size_t> _initial;
        ptrie::map<bool> _states;
        std::vector<const Interface*> _all_interfaces;
    };
}

#endif /* NETWORKPDA_H */
