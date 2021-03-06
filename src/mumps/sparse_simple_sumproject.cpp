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

#include<abcd.h>
#include<mumps.h>
#include<algorithm>
using namespace  boost::lambda;

void abcd::spSimpleProject(std::vector<int> mycols, std::vector<int> &vrows,
                           std::vector<int> &vcols, std::vector<double> &vvals)
{
    int s = mycols.size();
    std::vector<int> rr;
    std::vector<double> rv;
    std::vector<int> target, target_idx;

    std::vector<std::map<int,int> > loc_cols(nb_local_parts);

    int nzr_estim = 0;

    // The right-hand sides to be used with the direct solver 
    std::vector<CompRow_Mat_double> r(nb_local_parts);

    // r_k = A_k Y^T
    for(int k = 0; k < nb_local_parts; k++) {

        CompRow_Mat_double Y;

        std::vector<int> yr(mycols.size());
        std::vector<int> yc(mycols.size());
        std::vector<double> yv(mycols.size(), 1);
        
        int c;

        int ct = 0;

        for(size_t i = 0; i < mycols.size(); i++){
            c = mycols[i];
            if(glob_to_part[k].find(n_o + c) != glob_to_part[k].end()){
                yr[ct] = glob_to_part[k][n_o + c];
                yc[ct] = i;

                ct++;
                loc_cols[k][i] = 1;
            }
        }

        Coord_Mat_double Yt(partitions[k].dim(1),
                            s, ct, &yv[0], &yr[0], &yc[0]);

        Y = CompRow_Mat_double(Yt);

        r[k] = spmm(partitions[k], Y) ;
        nzr_estim += r[k].NumNonzeros();
    }


    // sparse mumps rhs
    mumps.setIcntl(20, 1);

    if(intra_comm.size() > 1){
        // distributed solution
        mumps.setIcntl(21, 1);
        mumps.lsol_loc = mumps.getInfo(23);
        mumps.sol_loc = new double[mumps.lsol_loc * s];
        mumps.isol_loc = new int[mumps.lsol_loc];
    } else {
        // Build the mumps rhs
        mumps.rhs = new double[mumps.n * s];
        mumps.lrhs = mumps.n;
        for(int i = 0; i < mumps.n * s; i++) mumps.rhs[i] = 0;
        MV_ColMat_double mumps_rhs(mumps.rhs, mumps.n, s, MV_Matrix_::ref);
    }

    {
        mumps.irhs_ptr      = new int[s + 1];
        rr.reserve(nzr_estim);
        rv.reserve(nzr_estim);

        int cnz = 1;


        for(int r_p = 0; r_p < s; r_p++) {
            mumps.irhs_ptr[r_p] = cnz;

            int pos = 0;
            for(int k = 0; k < nb_local_parts; k++) {
                CompRow_Mat_double & rtt = r[k];
                int _dim1 = partitions[k].dim(1);
                int _dim0 = partitions[k].dim(0);

                int j = 0;
                for(int i = pos + _dim1; i < pos + _dim1 + _dim0; i++) {
                    if(rtt(j, r_p) != 0){
                        rr.push_back(i+1);
                        rv.push_back(rtt(j, r_p));
                        cnz++;
                    }
                    j++;
                }
                pos += _dim1 + _dim0;
            }
        }
        mumps.irhs_ptr[s] = cnz;

        mumps.nz_rhs        = cnz - 1;

        mumps.rhs_sparse    = &rv[0];
        mumps.irhs_sparse   = &rr[0];
    }

    

    if (intra_comm.size() > 1) {
        int job = 3;
        mpi::broadcast(intra_comm, job, 0);
        mpi::broadcast(intra_comm, s, 0);

        // we are sure here that we have only a single partition
        int start_c;
        if (stC[0] != -1) {
            start_c = glob_to_part[0][stC[0]];
            mpi::broadcast(intra_comm, start_c, 0);
        } else {
            start_c = -1;
            mpi::broadcast(intra_comm, start_c, 0);
            return ;
        }

        mpi::broadcast(intra_comm, column_index, 0);

        std::vector<int> locc(loc_cols[0].size(), 0);
        for(size_t i = 0; i < locc.size(); i++){
            if(loc_cols[0][i]){
                locc[i] = 1;
            }
        }
        mpi::broadcast(intra_comm, locc, 0);
        mpi::broadcast(intra_comm, n_o, 0);
        mpi::broadcast(intra_comm, mycols, 0);
    }

    mumps.nrhs          = s;
    mumps.job           = 3;

    dmumps_c(&mumps);

    double *sol_ptr;
    int sol_lda, ci, col, x_pos = 0, start_c;
    double val;
    if(intra_comm.size() > 1){
        int part = 0;
        sol_lda = mumps.lsol_loc;
        std::vector<int> &civ = column_index[part];

        // get where we should look for the current part
        if (stC[part] != -1) {
            start_c = glob_to_part[part][stC[part]];
        } else {
            start_c = civ.size();
        }
        x_pos = start_c;
        int end_c = civ.size();

        if(target.size() == 0){
            for(int i_loc = 0; i_loc < mumps.lsol_loc; i_loc++) {
                int isol = mumps.isol_loc[i_loc] - 1;
                int ci = civ[isol] - n_o; 
                if (isol >= start_c && isol < end_c){
                    target.push_back(i_loc);
                    target_idx.push_back(ci);
                }
            }
        }

        sol_ptr = mumps.sol_loc;
        for (int j = 0; j < s; j++) {
            col = mycols[j];
            for (int i = 0; i < (int)target.size(); i++) {

                val = -sol_ptr[target[i] + j * sol_lda];
                ci = target_idx[i];

                if (ci == col) val += (double)0.5;

                if(ci >= col && val != 0){
                    vvals.push_back(val);
                    vrows.push_back(ci + 1);
                    vcols.push_back(col + 1);
                }
            }
        }
    } else {
        sol_lda = mumps.lrhs;
        sol_ptr = mumps.rhs;

        for(int part = 0; part < nb_local_parts; part++){
            int start_c;

            std::vector<int> &civ = column_index[part];

            // get where we should look for the current part
            if (stC[part] != -1) {
                start_c = glob_to_part[part][stC[part]];
            } else {
                start_c = civ.size();
            }
            x_pos += start_c;

            vrows.reserve((civ.size() - start_c)*s);
            vcols.reserve((civ.size() - start_c)*s);
            vvals.reserve((civ.size() - start_c)*s);

            int old_x = x_pos;
            for(int j = 0; j < s; j++){
                col = mycols[j];
                x_pos = old_x;

                for(size_t i = start_c; i < civ.size(); i++){
                    ci = civ[i] - n_o;

                    val =  - sol_ptr[x_pos + j * sol_lda];

                    if (ci == col)
                      val += (double)0.5;

                    if(ci >= col && val != 0){

                        vvals.push_back(val);
                        vrows.push_back(ci + 1);
                        vcols.push_back(col + 1);
                    }


                    x_pos++;
                }

            }

            // move to the next partition
            x_pos += partitions[part].dim(0);
        }
    }



    // disable sparse mumps rhs
    mumps.setIcntl(20, 0);
    mumps.setIcntl(21, 0);
    if(intra_comm.size() > 1){
        delete[] mumps.isol_loc;
        delete[] mumps.sol_loc;
    } else {
        delete[] mumps.rhs;
    }
}
