// Copyright Institut National Polytechnique de Toulouse (2014) 
// Contributor(s) :
// M. Zenadi <mzenadi@enseeiht.fr>
// D. Ruiz <ruiz@enseeiht.fr>
// R. Guivarch <guivarch@enseeiht.fr>

// This software is governed by the CeCILL-C license under French law and
// abiding by the rules of distribution of free software.  You can  use, 
// modify and/ or redistribute the software under the terms of the CeCILL-C
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info/licences/Licence_CeCILL-C_V1-en.html"

// As a counterpart to the access to the source code and  rights to copy,
// modify and redistribute granted by the license, users are provided only
// with a limited warranty  and the software's author,  the holder of the
// economic rights,  and the successive licensors  have only  limited
// liability. 

// In this respect, the user's attention is drawn to the risks associated
// with loading,  using,  modifying and/or developing or reproducing the
// software by the user in light of its specific status of free software,
// that may mean  that it is complicated to manipulate,  and  that  also
// therefore means  that it is reserved for developers  and  experienced
// professionals having in-depth computer knowledge. Users are therefore
// encouraged to load and test the software's suitability as regards their
// requirements in conditions enabling the security of their systems and/or 
// data to be ensured and,  more generally, to use and operate it in the 
// same conditions as regards security. 

// The fact that you are presently reading this means that you have had
// knowledge of the CeCILL-C license and that you accept its terms.

#include <mumps.h>
#include <abcd.h>

void abcd::initializeMumps(MUMPS &mu)
{
    initializeMumps(mu, false);
}

void abcd::initializeMumps(MUMPS &mu, bool local)
{
    // The first run of MUMPS is local to CG-masters
    std::vector<int> r;
    if(local) {
        r.push_back(inter_comm.rank());
        mpi::group grp = inter_comm.group().include(r.begin(), r.end());
        intra_comm = mpi::communicator(inter_comm, grp);
    } else {
        if(instance_type == 0) {
            r.push_back(comm.rank());
        } else {
            r.push_back(my_master);
        }
        std::copy(my_slaves.begin(), my_slaves.end(), std::back_inserter(r));

        mpi::group grp = comm.group().include(r.begin(), r.end());
        intra_comm = mpi::communicator(comm, grp);
    }

    mu.sym = 2;
    mu.par = 1;
    mu.job = -1;
    mu.comm_fortran = MPI_Comm_c2f((MPI_Comm) intra_comm);

    dmumps_c(&mu);
    if(mu.getInfo(1) != 0) throw mu.getInfo(1);

    mu.initialized = true;

    mu.setIcntl(1, -1);
    mu.setIcntl(2, -1);
    mu.setIcntl(3, -1);

    mu.setIcntl(6, 5);
    mu.setIcntl(7, 5);
    mu.setIcntl(8, -2);
    mu.setIcntl(12, 2);
    mu.setIcntl(14, 90);
}
