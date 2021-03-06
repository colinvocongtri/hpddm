/*
   This file is part of HPDDM.

   Author(s): Pierre Jolivet <pierre.jolivet@inf.ethz.ch>
              Frédéric Nataf <nataf@ann.jussieu.fr>
        Date: 2012-12-15

   Copyright (C) 2011-2014 Université de Grenoble
                 2015      Eidgenössische Technische Hochschule Zürich

   HPDDM is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published
   by the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   HPDDM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with HPDDM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SUBDOMAIN_
#define _SUBDOMAIN_

#include <unordered_map>

namespace HPDDM {
/* Class: Subdomain
 *
 *  A class for handling all communications and computations between subdomains.
 *
 * Template Parameter:
 *    K              - Scalar type. */
template<class K>
class Subdomain {
    protected:
        /* Variable: rbuff
         *  Vector used as the receiving buffer for point-to-point communications with neighboring subdomains. */
        std::vector<K*>          _rbuff;
        /* Variable: sbuff
         *  Vector used as the sending buffer for point-to-point communications with neighboring subdomains. */
        std::vector<K*>          _sbuff;
        /* Variable: rq
         *  Array of MPI requests to check completion of the MPI transfers with neighboring subdomains. */
        MPI_Request*                _rq;
        /* Variable: communicator
         *  MPI communicator of the subdomain. */
        MPI_Comm          _communicator;
        /* Variable: dof
         *  Number of degrees of freedom in the current subdomain. */
        int                        _dof;
        /* Variable: map */
        vectorNeighbor             _map;
        /* Variable: a
         *  Local matrix. */
        MatrixCSR<K>*                _a;
    public:
        Subdomain() : _rq(), _map(), _a() { }
        ~Subdomain() {
            delete _a;
            delete [] _rq;
            if(!_rbuff.empty())
                delete [] _rbuff[0];
        }
        /* Function: getCommunicator
         *  Returns a reference to <Subdomain::communicator>. */
        inline const MPI_Comm& getCommunicator() const { return _communicator; }
        /* Function: getMap
         *  Returns a reference to <Subdomain::map>. */
        inline const vectorNeighbor& getMap() const { return _map; }
        /* Function: exchange
         *
         *  Exchanges and reduces values of duplicated unknowns.
         *
         * Parameter:
         *    in             - Input vector. */
        inline void exchange(K* const in) const {
            for(unsigned short i = 0; i < _map.size(); ++i) {
                MPI_Irecv(_rbuff[i], _map[i].second.size(), Wrapper<K>::mpi_type(), _map[i].first, 0, _communicator, _rq + i);
                Wrapper<K>::gthr(_map[i].second.size(), in, _sbuff[i], _map[i].second.data());
                MPI_Isend(_sbuff[i], _map[i].second.size(), Wrapper<K>::mpi_type(), _map[i].first, 0, _communicator, _rq + _map.size() + i);
            }
            for(unsigned short i = 0; i < _map.size(); ++i) {
                int index;
                MPI_Waitany(_map.size(), _rq, &index, MPI_STATUS_IGNORE);
                for(unsigned int j = 0; j < _map[index].second.size(); ++j)
                    in[_map[index].second[j]] += _rbuff[index][j];
            }
            MPI_Waitall(_map.size(), _rq + _map.size(), MPI_STATUSES_IGNORE);
        }
        /* Function: recvBuffer
         *
         *  Exchanges values of duplicated unknowns.
         *
         * Parameter:
         *    in             - Input vector. */
        inline void recvBuffer(const K* const in) const {
            for(unsigned short i = 0; i < _map.size(); ++i) {
                MPI_Irecv(_rbuff[i], _map[i].second.size(), Wrapper<K>::mpi_type(), _map[i].first, 0, _communicator, _rq + i);
                Wrapper<K>::gthr(_map[i].second.size(), in, _sbuff[i], _map[i].second.data());
                MPI_Isend(_sbuff[i], _map[i].second.size(), Wrapper<K>::mpi_type(), _map[i].first, 0, _communicator, _rq + _map.size() + i);
            }
            MPI_Waitall(2 * _map.size(), _rq, MPI_STATUSES_IGNORE);
        }
        /* Function: initialize
         *
         *  Initializes all buffers for point-to-point communications and set internal pointers to user-defined values.
         *
         * Parameters:
         *    a              - Local matrix.
         *    o              - Indices of neighboring subdomains.
         *    r              - Local-to-neighbor mappings.
         *    comm           - MPI communicator of the domain decomposition. */
        template<class Neighbor, class Mapping>
        inline void initialize(MatrixCSR<K>* const& a, const Neighbor& o, const Mapping& r, MPI_Comm* const& comm = nullptr) {
            if(comm)
                _communicator = *comm;
            else
                _communicator = MPI_COMM_WORLD;
            _a = a;
            _dof = _a->_n;
            _map.resize(o.size());
            unsigned int size = 0;
            unsigned short j = 0;
            for(const auto& i : o) {
                _map[j].first = i;
                _map[j].second.reserve(r[j].size());
                for(int k = 0; k < r[j].size(); ++k)
                    _map[j].second.emplace_back(r[j][k]);
                size += _map[j++].second.size();
            }
            _rq = new MPI_Request[2 * _map.size()];
            _rbuff.reserve(_map.size());
            _sbuff.reserve(_map.size());
            if(size) {
                K* rbuff = new K[2 * size];
                K* sbuff = rbuff + size;
                size = 0;
                for(const auto& i : _map) {
                    _rbuff.emplace_back(rbuff + size);
                    _sbuff.emplace_back(sbuff + size);
                    size += i.second.size();
                }
            }
        }
        /* Function: initialize(dummy)
         *  Dummy function for masters excluded from the domain decomposition. */
        inline void initialize(MPI_Comm* const& comm = nullptr) {
            if(comm)
                _communicator = *comm;
            else
                _communicator = MPI_COMM_WORLD;
        }
        /* Function: exclusion
         *
         *  Checks whether <Subdomain::communicator> has been built by excluding some processes.
         *
         * Parameter:
         *    comm          - Reference MPI communicator. */
        inline bool exclusion(const MPI_Comm& comm) const {
            int result;
            MPI_Comm_compare(_communicator, comm, &result);
            return result != MPI_CONGRUENT;
        }
        /* Function: getDof
         *  Returns the value of <Subdomain::dof>. */
        inline int getDof() const { return _dof; }
        /* Function: getMatrix
         *  Returns a constant pointer to <Subdomain::a>. */
        inline const MatrixCSR<K>* getMatrix() const { return _a; }
        /* Function: interaction
         *
         *  Builds a vector of matrices to store interactions with neighboring subdomains.
         *
         * Template Parameters:
         *    N              - 0- or 1-based indexing of the input matrix.
         *    sorted         - True if the column indices of each matrix in the vector must be sorted.
         *    scale          - True if the matrices must be scaled by the neighboring partition of unity.
         *
         * Parameters:
         *    v              - Output vector.
         *    scaling        - Local partition of unity.
         *    pt             - Pointer to a <MatrixCSR>. */
        template<char N, bool sorted = true, bool scale = false>
        inline void interaction(std::vector<const MatrixCSR<K>*>& v, const typename Wrapper<K>::ul_type* const scaling = nullptr, const MatrixCSR<K, N>* const pt = nullptr) const {
            const MatrixCSR<K, N>& ref = pt ? *pt : *_a;
            if(ref._n != _dof || ref._m != _dof)
                std::cerr << "Problem with the input matrix" << std::endl;
            std::vector<std::vector<std::tuple<unsigned int, unsigned int, unsigned int>>> send(_map.size());
            unsigned int* sendSize = new unsigned int[4 * _map.size()];
            unsigned int* recvSize = sendSize + 2 * _map.size();
            for(unsigned short k = 0; k < _map.size(); ++k)
                MPI_Irecv(recvSize + 2 * k, 2, MPI_UNSIGNED, _map[k].first, 10, _communicator, _rq + k);
            for(unsigned short k = 0; k < _map.size(); ++k) {
                std::vector<std::pair<unsigned int, unsigned int>> fast;
                fast.reserve(_map[k].second.size());
                for(unsigned int i = 0; i < _map[k].second.size(); ++i)
                    fast.emplace_back(_map[k].second[i], i);
                std::sort(fast.begin(), fast.end());
                std::vector<std::pair<unsigned int, unsigned int>>::const_iterator itRow = fast.cbegin();
                for(unsigned int i = 0; i < _dof; ++i) {
                    std::vector<std::pair<unsigned int, unsigned int>>::const_iterator begin = fast.cbegin();
                    if(itRow != fast.cend() && itRow->first == i) {
                        if(ref._sym) {
                            for(unsigned int j = ref._ia[i]; j < ref._ia[i + 1]; ++j) {
                                std::vector<std::pair<unsigned int, unsigned int>>::const_iterator it = std::lower_bound(begin, fast.cend(), std::make_pair(ref._ja[j], 0), [](const std::pair<unsigned int, unsigned int>& lhs, const std::pair<unsigned int, unsigned int>& rhs) { return lhs.first < rhs.first; });
                                if(it == fast.cend() || ref._ja[j] < it->first)
                                    send[k].emplace_back(itRow->second, ref._ja[j], j - (N == 'F'));
                                else
                                    begin = it;
                            }
                        }
                        ++itRow;
                    }
                    else {
                        for(unsigned int j = ref._ia[i]; j < ref._ia[i + 1]; ++j) {
                            std::vector<std::pair<unsigned int, unsigned int>>::const_iterator it = std::lower_bound(begin, fast.cend(), std::make_pair(ref._ja[j], 0), [](const std::pair<unsigned int, unsigned int>& lhs, const std::pair<unsigned int, unsigned int>& rhs) { return lhs.first < rhs.first; });
                            if(it != fast.cend() && !(ref._ja[j] < it->first)) {
                                send[k].emplace_back(it->second, i, j - (N == 'F'));
                                begin = it;
                            }
                        }
                    }
                }
                std::sort(send[k].begin(), send[k].end());
                sendSize[2 * k] = send[k].empty() ? 0 : 1;
                for(unsigned int i = 1; i < send[k].size(); ++i)
                    if(std::get<0>(send[k][i]) != std::get<0>(send[k][i - 1]))
                        ++sendSize[2 * k];
                sendSize[2 * k + 1] = std::ceil((sendSize[2 * k] * sizeof(unsigned short)
                                    + (sendSize[2 * k] + 1 + send[k].size()) * sizeof(unsigned int)) / static_cast<float>(sizeof(K)))
                                    + send[k].size();
                MPI_Isend(sendSize + 2 * k, 2, MPI_UNSIGNED, _map[k].first, 10, _communicator, _rq + _map.size() + k);
            }
            MPI_Waitall(2 * _map.size(), _rq, MPI_STATUSES_IGNORE);
            unsigned short maxRecv = 0;
            unsigned int accumulate = 0;
            while(maxRecv < _map.size()) {
                unsigned int next = accumulate + recvSize[2 * maxRecv + 1];
                if(next < std::distance(_rbuff[0], _sbuff.back()) + _map.back().second.size())
                    accumulate = next;
                else
                    break;
                ++maxRecv;
            }
            unsigned short maxSend = 0;
            if(maxRecv == _map.size())
                while(maxSend < _map.size()) {
                    unsigned int next = accumulate + sendSize[2 * maxSend + 1];
                    if(next < std::distance(_rbuff[0], _sbuff.back()) + _map.back().second.size())
                        accumulate = next;
                    else
                        break;
                    ++maxSend;
                }
            std::vector<K*> rbuff;
            rbuff.reserve(_map.size());
            accumulate = 0;
            for(unsigned int k = 0; k < _map.size(); ++k) {
                if(k < maxRecv) {
                    rbuff.emplace_back(_rbuff[0] + accumulate);
                    accumulate += recvSize[2 * k + 1];
                }
                else if(k == maxRecv) {
                    unsigned int accumulateSend = 0;
                    for(unsigned short j = k; j < _map.size(); ++j)
                        accumulateSend += recvSize[2 * j + 1];
                    accumulate += accumulateSend;
                    for(unsigned short j = 0; j < _map.size(); ++j)
                        accumulateSend += sendSize[2 * j + 1];
                    rbuff.emplace_back(new K[accumulateSend]);
                }
                else
                    rbuff.emplace_back(rbuff.back() + recvSize[2 * k - 1]);
                MPI_Irecv(rbuff[k], recvSize[2 * k + 1], Wrapper<K>::mpi_type(), _map[k].first, 100, _communicator, _rq + k);
            }
            std::vector<K*> sbuff;
            sbuff.reserve(_map.size());
            for(unsigned short k = 0; k < _map.size(); ++k) {
                if(maxRecv < _map.size()) {
                    if(k == 0)
                        sbuff.emplace_back(rbuff.back() + recvSize[2 * _map.size() - 1]);
                    else
                        sbuff.emplace_back(sbuff.back() + sendSize[2 * k - 1]);
                }
                else if(k < maxSend) {
                    sbuff.emplace_back(rbuff[0] + accumulate);
                    accumulate += sendSize[2 * k + 1];
                }
                else if(k == maxSend) {
                    unsigned int accumulateTotal = accumulate;
                    for(unsigned int j = k; j < _map.size(); ++j)
                        accumulateTotal += sendSize[2 * j + 1];
                    sbuff.emplace_back(new K[accumulateTotal]);
                }
                else
                    sbuff.emplace_back(sbuff.back() + sendSize[2 * k - 1]);
                unsigned short* ia = reinterpret_cast<unsigned short*>(sbuff[k]);
                unsigned int* mapRow = reinterpret_cast<unsigned int*>(ia + sendSize[2 * k]);
                unsigned int* ja = mapRow + sendSize[2 * k] + 1;
                K* a = sbuff[k] + sendSize[2 * k + 1] - send[k].size();
                *mapRow++ = send[k].size();
                if(!send[k].empty()) {
                    *mapRow++ = std::get<0>(send[k][0]);
                    unsigned int prev = 0;
                    for(unsigned int i = 0; i < send[k].size(); ++i) {
                        if(i > 0 && std::get<0>(send[k][i]) != std::get<0>(send[k][i - 1])) {
                            *ia++ = static_cast<unsigned short>(i - prev);
                            prev = i;
                            *mapRow++ = std::get<0>(send[k][i]);
                        }
                        *ja++ = std::get<1>(send[k][i]);
                        *a = ref._a[std::get<2>(send[k][i])];
                        if(scale && scaling)
                            *a *= scaling[ref._ja[std::get<2>(send[k][i])]];
                        ++a;
                    }
                    *ia++ = send[k].size() - prev;
                }
                MPI_Isend(sbuff[k], sendSize[2 * k + 1], Wrapper<K>::mpi_type(), _map[k].first, 100, _communicator, _rq + _map.size() + k);
            }
            decltype(send)().swap(send);
            if(!v.empty())
                v.clear();
            v.reserve(_map.size());
            for(unsigned short k = 0; k < _map.size(); ++k) {
                int index;
                MPI_Waitany(_map.size(), _rq, &index, MPI_STATUS_IGNORE);
                unsigned short* ia = reinterpret_cast<unsigned short*>(rbuff[index]);
                unsigned int* mapRow = reinterpret_cast<unsigned int*>(ia + recvSize[2 * index]);
                unsigned int* ja = mapRow + recvSize[2 * index] + 1;
                const unsigned int nnz = *mapRow++;
                K* a = rbuff[index] + recvSize[2 * index + 1] - nnz;
                std::unordered_map<unsigned int, unsigned int> mapCol;
                mapCol.reserve(nnz);
                for(unsigned int i = 0, j = 0; i < nnz; ++i)
                    if(mapCol.count(ja[i]) == 0)
                        mapCol[ja[i]] = j++;
                MatrixCSR<K>* AIJ = new MatrixCSR<K>(_dof, mapCol.size(), nnz, false);
                v.emplace_back(AIJ);
                std::fill(AIJ->_ia, AIJ->_ia + AIJ->_n + 1, 0);
                for(unsigned int i = 0; i < recvSize[2 * index]; ++i) {
#if 0
                    if(std::abs(scaling[_map[index].second[mapRow[i]]]) > HPDDM_EPS)
                        std::cerr << "Problem with the partition of unity: (std::abs(d[" << _map[index].second[mapRow[i]] << "]) = " << std::abs(scaling[_map[index].second[mapRow[i]]]) << ") > HPDDM_EPS" << std::endl;
#endif
                    AIJ->_ia[_map[index].second[mapRow[i]] + 1] = ia[i];
                }
                std::partial_sum(AIJ->_ia, AIJ->_ia + AIJ->_n + 1, AIJ->_ia);
                if(AIJ->_ia[AIJ->_n] != nnz)
                    std::cerr << "Problem with the received CSR: (AIJ->_ia[" << AIJ->_n << "] = " << AIJ->_ia[AIJ->_n] << ") != " << nnz << std::endl;
                for(unsigned int i = 0, m = 0; i < recvSize[2 * index]; ++i) {
                    unsigned int pos = AIJ->_ia[_map[index].second[mapRow[i]]];
                    for(unsigned short j = 0; j < ia[i]; ++j, ++m) {
                        AIJ->_ja[pos + j] = mapCol[ja[m]];
                        AIJ->_a[pos + j] = a[m];
                    }
                    if(sorted) {
                        std::vector<unsigned short> idx;
                        idx.reserve(ia[i]);
                        for(unsigned short j = 0; j < ia[i]; ++j)
                            idx.emplace_back(j);
                        std::sort(idx.begin(), idx.end(), [&](const unsigned short& lhs, const unsigned short& rhs) { return AIJ->_ja[pos + lhs] < AIJ->_ja[pos + rhs]; });
                        reorder(idx, AIJ->_ja + pos, AIJ->_a + pos);
                    }
                }
            }
            MPI_Waitall(_map.size(), _rq + _map.size(), MPI_STATUSES_IGNORE);
            delete [] sendSize;
            if(maxRecv < _map.size())
                delete [] rbuff[maxRecv];
            else if(maxSend < _map.size())
                delete [] sbuff[maxSend];
        }
        /* Function: globalMapping
         *
         *  Computes a global numbering of all unknowns.
         *
         * Template Parameters:
         *    N              - 0- or 1-based indexing.
         *    It             - Random iterator.
         *
         * Parameters:
         *    first         - First element of the list of local unknowns with the global numbering.
         *    last          - Last element of the list of local unknowns with the global numbering.
         *    start         - Lowest global number of the local unknowns.
         *    end           - Highest global number of the local unknowns.
         *    global        - Global number of unknowns.
         *    d             - Local partition of unity (optional). */
        template<char N, class It>
        inline void globalMapping(It first, It last, unsigned int& start, unsigned int& end, unsigned int& global, const typename Wrapper<K>::ul_type* const d = nullptr) const {
            unsigned int between = 0;
            int rankWorld, sizeWorld;
            MPI_Comm_rank(_communicator, &rankWorld);
            MPI_Comm_size(_communicator, &sizeWorld);
            if(sizeWorld > 1) {
                for(unsigned short i = 0; i < _map.size() && _map[i].first < rankWorld; ++i)
                    ++between;
                unsigned int size = std::ceil(2 * (std::distance(_sbuff.front(), _sbuff.back()) + _map.back().second.size() + 1) * sizeof(unsigned int) / static_cast<float>(sizeof(K)));
                unsigned int* rbuff = (size < std::distance(_rbuff[0], _sbuff.back()) + _map.back().second.size() ? reinterpret_cast<unsigned int*>(_rbuff[0]) : new unsigned int[2 * (std::distance(_sbuff.front(), _sbuff.back()) + _map.back().second.size() + 1)]);
                unsigned int* sbuff = rbuff + std::distance(_sbuff.front(), _sbuff.back()) + _map.back().second.size() + 1;
                size = 0;
                MPI_Request* rq = new MPI_Request[2];

                for(unsigned short i = 0; i < between; ++i) {
                    MPI_Irecv(rbuff + size, _map[i].second.size() + (_map[i].first == rankWorld - 1), MPI_UNSIGNED, _map[i].first, 10, _communicator, _rq + i);
                    size += _map[i].second.size();
                }

                if(rankWorld && ((between && _map[between - 1].first != rankWorld - 1) || !between))
                    MPI_Irecv(rbuff + size, 1, MPI_UNSIGNED, rankWorld - 1, 10, _communicator, rq);
                else
                    rq[0] = MPI_REQUEST_NULL;

                ++size;
                for(unsigned short i = between; i < _map.size(); ++i) {
                    MPI_Irecv(rbuff + size, _map[i].second.size(), MPI_UNSIGNED, _map[i].first, 10, _communicator, _rq + _map.size() + i);
                    size += _map[i].second.size();
                }

                unsigned int begining;
                std::fill(first, last, std::numeric_limits<unsigned int>::max());
                if(rankWorld == 0) {
                    begining = static_cast<unsigned int>(N == 'F');
                    start = begining;
                    for(unsigned int i = 0; i < std::distance(first, last); ++i)
                        if(!d || d[i] > 0.1)
                            *(first + i) = begining++;
                    end = begining;
                }
                size = 0;
                for(unsigned short i = 0; i < between; ++i) {
                    MPI_Wait(_rq + i, MPI_STATUS_IGNORE);
                    for(unsigned int j = 0; j < _map[i].second.size(); ++j)
                        first[_map[i].second[j]] = rbuff[size + j];
                    size += _map[i].second.size();
                }
                if(rankWorld) {
                    if((between && _map[between - 1].first != rankWorld - 1) || !between)
                        MPI_Wait(rq, MPI_STATUS_IGNORE);
                    begining = rbuff[size];
                    start = begining;
                    for(unsigned int i = 0; i < std::distance(first, last); ++i)
                        if((!d || d[i] > 0.1) && *(first + i) == std::numeric_limits<unsigned int>::max())
                            *(first + i) = begining++;
                    end = begining;
                }
                size = 0;
                if(rankWorld != sizeWorld - 1) {
                    if(_map[between].first == rankWorld + 1)
                        sbuff[_map[between].second.size()] = begining;
                    for(unsigned short i = between; i < _map.size(); ++i) {
                        for(unsigned short j = 0; j < _map[i].second.size(); ++j)
                            sbuff[size + j] = *(first + _map[i].second[j]);
                        MPI_Isend(sbuff + size, _map[i].second.size() + (_map[i].first == rankWorld + 1), MPI_UNSIGNED, _map[i].first, 10, _communicator, _rq + i);
                        size += _map[i].second.size() + (_map[i].first == rankWorld + 1);
                    }
                    if(_map[between].first != rankWorld + 1)
                        MPI_Isend(&begining, 1, MPI_UNSIGNED, rankWorld + 1, 10, _communicator, rq + 1);
                    else
                        rq[1] = MPI_REQUEST_NULL;
                }
                else
                    rq[1] = MPI_REQUEST_NULL;
                unsigned int stop = 0;
                for(unsigned short i = 0; i < between; ++i) {
                    for(unsigned short j = 0; j < _map[i].second.size(); ++j)
                        sbuff[size + j] = *(first + _map[i].second[j]);
                    MPI_Isend(sbuff + size, _map[i].second.size(), MPI_UNSIGNED, _map[i].first, 10, _communicator, _rq + _map.size() + i);
                    size += _map[i].second.size();
                    stop += _map[i].second.size();
                }
                ++stop;
                for(unsigned short i = between; i < _map.size(); ++i) {
                    MPI_Wait(_rq + _map.size() + i, MPI_STATUS_IGNORE);
                    for(unsigned int j = 0; j < _map[i].second.size(); ++j)
                        first[_map[i].second[j]] = rbuff[stop + j];
                    stop += _map[i].second.size();
                }
                MPI_Waitall(_map.size(), _rq + between, MPI_STATUSES_IGNORE);
                MPI_Waitall(2, rq, MPI_STATUSES_IGNORE);
                delete [] rq;
                if(rbuff != reinterpret_cast<unsigned int*>(_rbuff[0]))
                    delete [] rbuff;
                global = end - (N == 'F');
                MPI_Bcast(&global, 1, MPI_UNSIGNED, sizeWorld - 1, _communicator);
            }
            else {
                std::iota(first, last, static_cast<unsigned int>(N == 'F'));
                start = (N == 'F');
                end = std::distance(first, last);
                global = end - start;
            }
        }
        /* Function: distributedCSR
         *  Assembles a distributed matrix that can be used by a backend such as PETSc.
         *
         * See also: <Subdomain::globalMapping>. */
        inline bool distributedCSR(unsigned int* num, unsigned int first, unsigned int last, int*& ia, int*& ja, K*& c, const MatrixCSR<K>* const& A) const {
            if(first != 0 || last != A->_n) {
                unsigned int nnz = 0;
                unsigned int dof = 0;
                for(unsigned int i = 0; i < A->_n; ++i) {
                    if(num[i] >= first && num[i] < last)
                        ++dof;
                }
                std::vector<std::vector<std::pair<unsigned int, K>>> tmp(dof);
                for(unsigned int i = 0; i < A->_n; ++i) {
                    if(num[i] >= first && num[i] < last)
                            tmp[num[i] - first].reserve(A->_ia[i + 1] - A->_ia[i]);
                }
                for(unsigned int i = 0; i < A->_n; ++i) {
                    if(num[i] >= first && num[i] < last) {
                        for(unsigned int j = A->_ia[i]; j < A->_ia[i + 1]; ++j)
                            tmp[num[i] - first].emplace_back(num[A->_ja[j]], A->_a[j]);
                    }
                }
                nnz = std::accumulate(tmp.cbegin(), tmp.cend(), 0, [](unsigned int sum, const std::vector<std::pair<unsigned int, K>>& v) { return sum + v.size(); });
                if(!c)
                    c  = new K[nnz];
                if(!ia)
                    ia = new int[dof + 1];
                if(!ja)
                    ja = new int[nnz];
                ia[0] = 0;
                nnz = 0;
                for(unsigned int i = 0; i < dof; ++i) {
                    std::sort(tmp[i].begin(), tmp[i].end());
                    for(std::pair<unsigned int, K>& p : tmp[i]) {
                        ja[nnz] = p.first;
                        c[nnz++] = p.second;
                    }
                    ia[i + 1] = nnz;
                }
                return true;
            }
            else {
                c  = A->_a;
                ia = A->_ia;
                ja = A->_ja;
                return false;
            }
        }
        /* Function: distributedVec
         *  Assembles a distributed vector that can by used by a backend such as PETSc.
         *
         * See also: <Subdomain::globalMapping>. */
        template<bool T>
        inline void distributedVec(unsigned int* num, unsigned int first, unsigned int last, K* const& in, K*& out, unsigned int n) const {
            if(first != 0 || last != n) {
                unsigned int dof = 0;
                for(unsigned int i = 0; i < n; ++i) {
                    if(num[i] >= first && num[i] < last)
                        ++dof;
                }
                if(!out)
                    out = new K[dof];
                for(unsigned int i = 0; i < n; ++i) {
                    if(num[i] >= first && num[i] < last) {
                        if(!T)
                            out[num[i] - first] = in[i];
                        else
                            in[i] = out[num[i] - first];
                    }
                }
            }
            else {
                if(!T)
                    std::copy(in, in + n, out);
                else
                    std::copy(out, out + n, in);
            }
        }
};
} // HPDDM
#endif // _SUBDOMAIN_
