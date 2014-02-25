#include <abcd.h>
#include <yasl/coo.hpp>
#include <yasl/csr.hpp>

using namespace std;


/// Set the defaults in the constructor
abcd::abcd()
{
    nrhs = 1;
    start_index = 0;
    block_size = 1;
    use_xk = false;
    use_xf = false;
    rhs = NULL;
    size_c = 0;
    verbose = false;
    threshold = 1e-12;
    runSolveS = false;
    guessPartitionsNumber = 0;
    for (int i = 0; i < 20; i++) {
        icntl[i] = 0;
        dcntl[i] = 0;
        info[i] = 0;
        dinfo[i] = 0;
    }
    
    irn = 0;
    jcn = 0;
    val = 0;

    icntl[14] = 16;
}

abcd::~abcd()
{
}

///
/// \brief Creates the internal matrix from user's data
///
int abcd::initializeMatrix()
{
    mpi::communicator world;
    
    if(world.rank() != 0) return 0;
    
    // Check that the matrix data is present
    if(irn == 0 || jcn == 0 || val == 0) {
        // Hey!! where is my data?
        throw - 1;
    }


    double t = MPI_Wtime();

    t = MPI_Wtime();
    Coord_Mat_double t_A;

    if(sym) {
        //over estimate nz
        int     *t_irn = new int[2*nz];
        int     *t_jcn = new int[2*nz];
        double  *t_val = new double[2*nz];
        int     t_nz = 0;
        for(int k = 0; k < nz; ++k) {
            irn[k]--; jcn[k]--;

            t_irn[t_nz] = irn[k];
            t_jcn[t_nz] = jcn[k];
            t_val[t_nz] = val[k];
            t_nz++;
            if(irn[k] != jcn[k]){
                t_irn[t_nz] = jcn[k];
                t_jcn[t_nz] = irn[k];
                t_val[t_nz] = val[k];
                t_nz++;
            }
        }
        nz = t_nz;
        t = MPI_Wtime();
        t_A = Coord_Mat_double(m, n, t_nz, t_val, t_irn, t_jcn);

        cout << "before splib : " << MPI_Wtime() - t << endl;
        t = MPI_Wtime();
        CooMatrix<> T(m, n, nz, t_irn, t_jcn, t_val);
        cout << "yasl : " << MPI_Wtime() - t << endl;

        t = MPI_Wtime();
        CsrMatrix<> M = T.toCSR();
        cout << "convert yasl : " << MPI_Wtime() - t << endl;
        delete[] t_irn;
        delete[] t_jcn;
        delete[] t_val;
    } else {
        //t = MPI_Wtime();
        //CooMatrix<> T(m, n, nz, irn, jcn, val, false, true);
        //cout << "yasl : " << MPI_Wtime() - t << endl;

        //t = MPI_Wtime();
        //CsrMatrix<> M(T);
        //cout << "convert yasl : " << MPI_Wtime() - t << endl;

        t = MPI_Wtime();
        for(int i=0; i<nz; i++){
            irn[i]--;
            jcn[i]--;
        }
        t_A = Coord_Mat_double(m, n, nz, val, irn, jcn);
        cout << "before splib : " << MPI_Wtime() - t << endl;
    }
    t = MPI_Wtime();
    A = CompRow_Mat_double(t_A);
    cout << "convert splib : " << MPI_Wtime() - t << endl;


    //exit(0);
    
    n_o = n;
    m_o = m;
    nz_o = nz;
        
    return 0; 
}

///
/// \brief Scales, partitions and analyses the structure of partitions
///
int abcd::preprocessMatrix()
{
    mpi::communicator world;
    if(world.rank() != 0) return 0;
    
    abcd::partitionMatrix();
    
    double timeToPreprocess = MPI_Wtime();
    abcd::preprocess();
    abcd::analyseFrame();
    cout << "Time for preprocess : " << MPI_Wtime() - timeToPreprocess << endl;
    return 0;
}

///
/// \brief Creates augmented systems and factorizes them
///
int abcd::factorizeAugmentedSystems()
{
    // Create the group of CG instances
    abcd::createInterComm();

    double t = MPI_Wtime();
    
    if(instance_type == 0) {
        // some data on augmented system
        mpi::broadcast(inter_comm, m_o, 0);
        mpi::broadcast(inter_comm, n_o, 0);
        mpi::broadcast(inter_comm, nz_o, 0);
        mpi::broadcast(inter_comm, icntl, 20, 0);
        mpi::broadcast(inter_comm, dcntl, 20, 0);
        if( icntl[10] != 0 )
            mpi::broadcast(inter_comm, size_c, 0);
    }

    if(instance_type == 0) {
        //abcd::distributePartitions();
        //IBARRIER; exit(0);

        abcd::distributeData();
        abcd::createInterconnections();
        //IBARRIER; exit(0);
    }
    
    abcd::initializeCimmino();
    
    if(IRANK == 0){
        clog << "[-]  Initialization time : " << MPI_Wtime() - t << endl;
    }
    if(inter_comm.rank() == 0 && instance_type == 0){
        cout << "[+] Launching MUMPS factorization" << endl;
    }

    t = MPI_Wtime();
    abcd::factorizeAugmentedSystems(mumps);
    
    if(IRANK == 0){
        cout << "[-]  Factorization time : " << MPI_Wtime() - t << endl;
    }

    return 0;
}

///
/// \brief Runs either BCG or ABCD solve depending on what we want
///
int abcd::solveSystem()
{
    mpi::communicator world;
    if(instance_type == 0) inter_comm.barrier();
    if(inter_comm.rank() == 0 && instance_type == 0){
        cout << "[+] Launching Solve" << endl;
    }
    if(instance_type == 0) {

        mpi::broadcast(inter_comm, icntl, 20, 0);
        mpi::broadcast(inter_comm, dcntl, 20, 0);

        if(size_c == 0 && inter_comm.rank() == 0 && icntl[10] != 0){
            cout << "Size of S is 0, therefore launching bcg" << endl;
            icntl[10] = 0;
        }

        abcd::distributeRhs();
        if(icntl[10] == 0 || icntl[12] != 0 || runSolveS){
            abcd::bcg(B);
        } else{
            int temp_bs = block_size;
            block_size = 1;
            abcd::solveABCD(B);
            block_size = temp_bs;
        }

        int job = -1;
        mpi::broadcast(intra_comm, job, 0);

        if(inter_comm.rank() == 0){
            clog << endl
                 << "======================================" << endl;
            clog << "Backward error : " << scientific << dinfo[0] << endl;
            if (Xf.dim(0) != 0)
                clog << "Forward error : " << scientific << dinfo[1] << endl;
            clog << "======================================" << endl << endl;
        }

    } else {
        abcd::waitForSolve();
    }
    
    return 0;
}
    
/// 
/// \brief The gateway function that launches all other options
///
int abcd::bc(int job)
{
    switch(job) {

    case -1:
        initializeMatrix();
        break;

    case 1:
        preprocessMatrix();
        break;

    case 2:
        factorizeAugmentedSystems();
        break;

    case 3:
        solveSystem();
        break;

    default:
        // Wrong job id
        return -1;
    }
    return 0;
}
