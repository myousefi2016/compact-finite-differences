from pycuda import autoinit
import pycuda.driver as cuda
import pycuda.gpuarray as gpuarray
import numpy as np
import sys
from near_toeplitz import *
from scipy.linalg import solve_banded
from numpy.testing import assert_allclose
import time

def scipy_solve_banded(a, b, c, rhs):
    '''
    Solve the tridiagonal system described
    by a, b, c, and rhs.
    a: lower off-diagonal array (first element ignored)
    b: diagonal array
    c: upper off-diagonal array (last element ignored)
    rhs: right hand side of the system
    '''
    l_and_u = (1, 1)
    ab = np.vstack([np.append(0, c[:-1]),
                    b,
                    np.append(a[1:], 0)])
    x = solve_banded(l_and_u, ab, rhs)
    return x

args = sys.argv
nx = int(args[1])
ny = int(args[2])
nz = int(args[3])

# generate the coefficient arrays and right-hand-side
d = np.ones([nz, ny, nx], dtype=np.float64)

# transfer to device
cfd = NearToeplitzSolver([nz, ny, nx], [1., 2., 1./4, 1., 1./4, 2., 1.])

start = cuda.Event()
end = cuda.Event()

for i in range(10):
    d_d = gpuarray.to_gpu(d)
    start.record()    
    cfd.solve(d_d, [1, 1])
    end.record()
    end.synchronize()
    x = d_d.get()
    print start.time_till(end)*1e-3

a = np.ones(nx, dtype=np.float64)*(1./4)
b = np.ones(nx, dtype=np.float64)*(1.)
c = np.ones(nx, dtype=np.float64)*(1./4)
a[-1] = 2.
c[0] = 2.

'''
x = d_d.get()

for i in range(nz):
    for j in range(ny):
        x_true = scipy_solve_banded(a, b, c, d[i, j, :])
        assert_allclose(x_true, x[i,j,:])
'''
