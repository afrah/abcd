// the solver's header file
#include "abcd.h"

// use boost::mpi for simplicity, the user can use which ever he wants
#include "mpi.h"
#include <boost/mpi.hpp>

/// we create a matrix for a regular 2D mesh + 5-point stencil 
void init_2d_lap(int m, int n, int nz, int *irn, int *jcn, double *val, int mesh_size);
void init_2d_lap(abcd &o, int mesh_size);

int main(int argc, char* argv[]) 
{
    // equivalent to MPI_Initialize
    mpi::environment env(argc, argv);

    // obtain the WORLD communicator, by default the solver uses it
    mpi::communicator world;

    // create one instance of the abcd solver per mpi-process
    abcd obj;

    if(world.rank() == 0) { // the master
        init_2d_lap(obj, 10);

        // set the rhs
        obj.rhs = new double[obj.m];
        for (size_t i = 0; i < obj.m; i++) {
            obj.rhs[i] = ((double) i + 1)/obj.m;
        }
    }

    try {
        obj(-1);
        obj(5); // equivalent to running 1, 2 and 3 successively
    } catch (runtime_error err) {
        cout << "An error occured: " << err.what() << endl;
    }

  return 0;
}

void init_2d_lap(abcd &obj, int mesh_size)
{
    obj.m = mesh_size*mesh_size; // number of rows
    obj.n = obj.m; // number of columns
    obj.nz = 3*obj.m - 2*mesh_size; // number of nnz in the lower-triangular part
    obj.sym = true;

    // allocate the arrays
    obj.irn = new int[obj.nz];
    obj.jcn = new int[obj.nz];
    obj.val = new double[obj.nz];

    init_2d_lap(obj.m, obj.n, obj.nz, obj.irn, obj.jcn, obj.val, mesh_size);
}

void init_2d_lap(int m, int n, int nz, int *irn, int *jcn, double *val, int mesh_size)
{

    // initialize the matrix
    // Note: the matrix is stored in 1-based format
    size_t pos = 0;
    for (size_t i = 1; i <= m; i++) {

        // the diagonal
        irn[pos] = i;
        jcn[pos] = i;
        val[pos] = 4.0;

        pos++;

        if (i == m) continue;
        // the lower-triangular part
        irn[pos] = i + 1;
        jcn[pos] = i;
        val[pos] = -1.0;
        pos++;

        if (i > m - 2*mesh_size + 1) continue;
        irn[pos] = i + mesh_size;
        jcn[pos] = i;
        val[pos] = -1.0;
        pos++;
    }

}