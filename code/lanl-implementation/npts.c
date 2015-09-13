#include <npts.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <sys/time.h>
#include <time.h>

void nonperiodic_tridiagonal_solver(MPI_Comm comm, double* beta_local,
    double* gam_local, double* r_local, size_t system_size, double* x_local)
{
    double *u_local, *phi_local, *psi_local;
    double *phi_lasts, *psi_lasts, *gam_firsts, *phi_firsts, *psi_firsts;
    double u_tilda, x_tilda, u_first, x_last;
    double product_1, product_2;
    int rank, nprocs;
    int local_size;
    int i, j;
    double tstart, tend, t1, t2;

    tstart = MPI_Wtime();

    t1 = MPI_Wtime();
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nprocs);
    local_size = system_size/nprocs;

    if (system_size%nprocs != 0) {
        printf("Error: system_size (%zd) not a multiple of nprocs (%d).\n", system_size, nprocs);
        return;
    }

    u_local = (double*) malloc(local_size*sizeof(double));
    phi_local = (double*) malloc(local_size*sizeof(double));
    psi_local = (double*) malloc(local_size*sizeof(double));
    phi_lasts = (double*) malloc(nprocs*sizeof(double));
    psi_lasts = (double*) malloc(nprocs*sizeof(double));
    phi_firsts = (double*) malloc(nprocs*sizeof(double));
    psi_firsts = (double*) malloc(nprocs*sizeof(double));
    gam_firsts = (double*) malloc(nprocs*sizeof(double));

    initZeros(u_local, local_size);

    /* --------- */
    /* L-R sweep */
    /* --------- */

    if (rank == 0) {
        phi_local[0] = 0.0;
        psi_local[0] = 1.0;
    }

    else {
        phi_local[0] = beta_local[0]*r_local[0];
        psi_local[0] = -(1./3)*beta_local[0];
    }

    for(i=1; i<local_size; i++) {
        phi_local[i] = beta_local[i]*(r_local[i] - (1./3)*phi_local[i-1]);
        psi_local[i] = -(1./3)*beta_local[i]*psi_local[i-1];
    }

    MPI_Barrier(comm);

    MPI_Allgather(&phi_local[local_size-1], 1, MPI_DOUBLE, phi_lasts,
        1, MPI_DOUBLE, comm);

    MPI_Allgather(&psi_local[local_size-1], 1, MPI_DOUBLE, psi_lasts,
        1, MPI_DOUBLE, comm);

    u_first = 0;
    if (rank == 0) {
        u_local[0] = beta_local[0]*r_local[0];
        u_tilda = u_local[0];
        u_first = u_local[0];
    }

    MPI_Bcast(&u_first, 1, MPI_DOUBLE, 0, comm);

    if (rank != 0) {
        u_tilda = 0.0;
        product_2 = 1.0;
        for (i=0; i<rank; i++) {
            product_1 = 1.0;
            for (j=i+1; j<rank; j++) {
                product_1 *= psi_lasts[j];
            }
            u_tilda += phi_lasts[i]*product_1;
            product_2 *= psi_lasts[i];
        }
        u_tilda += u_first*product_2;
    }

    MPI_Barrier(comm);

    for (i=0; i<local_size; i++) {
        u_local[i] = phi_local[i] + u_tilda*psi_local[i];
    }

    MPI_Barrier(comm);

    /* --------- */
    /* R-L sweep */
    /* --------- */

    initZeros(gam_firsts, nprocs);
    MPI_Allgather(gam_local, 1, MPI_DOUBLE, gam_firsts, 1, MPI_DOUBLE, comm);

    if (rank == nprocs-1) {
        phi_local[local_size-1] = 0.0;
        psi_local[local_size-1] = 1.0;
    }

    else {
        phi_local[local_size-1] = u_local[local_size-1];
        psi_local[local_size-1] = -gam_firsts[rank+1];
    }

    for (i=1; i<local_size; i++) {
        phi_local[local_size-1-i] = u_local[local_size-1-i] -
            gam_local[local_size-1-i+1]*phi_local[local_size-1-i+1];
        psi_local[local_size-1-i] = -gam_local[local_size-1-i+1]*psi_local[local_size-1-i+1];
    }

    MPI_Barrier(comm);

    MPI_Allgather(phi_local, 1, MPI_DOUBLE, phi_firsts, 1, MPI_DOUBLE, comm);
    MPI_Allgather(psi_local, 1, MPI_DOUBLE, psi_firsts, 1, MPI_DOUBLE, comm);

    x_last = 0.0;
    if (rank == nprocs-1) {
        x_local[local_size-1] = u_local[local_size-1];
        x_tilda = x_local[local_size-1];
        x_last = x_tilda;
    }

    MPI_Bcast(&x_last, 1, MPI_DOUBLE, nprocs-1, comm);

    if (rank != nprocs-1) {
        x_tilda = 0.0;
        for (i=rank+2; i<nprocs; i++) {
            product_1 = 1.0;
            for (j=rank+1; j<i; j++) {
                product_1 *= psi_firsts[j];
            }
            x_tilda += phi_firsts[i]*product_1;
        }

        product_2 = 1.0;
        for (i=rank+1; i<nprocs; i++) {
            product_2 *= psi_firsts[i];
        }

        x_tilda += phi_firsts[rank+1] + x_last*product_2;
    }

    MPI_Barrier(comm);

    for (i=0; i<local_size; i++) {
        x_local[local_size-1-i] = phi_local[local_size-1-i] +
            x_tilda*psi_local[local_size-1-i];
    }

    MPI_Barrier(comm);

    free(phi_lasts);
    free(psi_lasts);
    free(phi_local);
    free(psi_local);
    free(u_local);

    MPI_Barrier(comm);
    tend = MPI_Wtime();
}


void precompute_beta_gam(MPI_Comm comm, size_t system_size, double* beta_local,
    double* gam_local)
{
    int rank, nprocs;
    int local_size;
    double last_beta;
    int i, r;

    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nprocs);
    local_size = system_size/nprocs;

    for (r=0; r<nprocs; r++) {
        if (rank == r) {
            if (rank == 0) {
                beta_local[0] = 1.0;
                gam_local[0] = 0.0;
            }
            else {
                MPI_Recv(&last_beta, 1, MPI_DOUBLE, rank-1, 10, comm, MPI_STATUS_IGNORE);
                beta_local[0] = 1./(1. - (1./3)*last_beta*(1./3));
                gam_local[0] = last_beta*(1./3);
            }

            for (i=1; i<local_size; i++) {
                beta_local[i] = 1./(1. - (1./3)*beta_local[i-1]*(1./3));
                gam_local[i] = beta_local[i-1]*(1./3);
            }

            if (rank != nprocs-1) {
                MPI_Send(&beta_local[local_size-1], 1, MPI_DOUBLE, rank+1, 10, comm);
            }
        }
    }
}
