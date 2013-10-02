/**
 * @file   nonlinear_solver_implementation.cpp
 * @author William A. Perkins
 * @date   2013-10-02 07:49:10 d3g096
 * 
 * @brief  Abstract class NonlinearSolverImplementation implementation 
 * 
 * 
 */

#include <boost/mpi/collectives.hpp>
#include "nonlinear_solver_implementation.hpp"

namespace gridpack {
namespace math {

// -------------------------------------------------------------
// struct null_deleter
// -------------------------------------------------------------
struct null_deleter
{
  void operator()(void const *) const { }
};

// -------------------------------------------------------------
//  class NonlinearSolverImplementation
// -------------------------------------------------------------

// -------------------------------------------------------------
// NonlinearSolverImplementation:: constructors / destructor
// -------------------------------------------------------------
NonlinearSolverImplementation::NonlinearSolverImplementation(const parallel::Communicator& comm,
                                                             const int& local_size,
                                                             JacobianBuilder form_jacobian,
                                                             FunctionBuilder form_function)
  : parallel::Distributed(comm), 
    utility::Configurable("NonlinearSolver"), 
    utility::Uncopyable(),
    p_J(), p_F(), 
    p_X((Vector *)NULL, null_deleter()),  // pointer set by solve()
    p_jacobian(form_jacobian), 
    p_function(form_function)
{
  p_F.reset(new Vector(this->communicator(), local_size));
  int cols;
  all_reduce(this->communicator(), local_size, cols, std::plus<int>());
  // std::cout << this->processor_rank() << ": "
  //           << "NonlinearSolverImplementation: construct Jacobian matrix: "
  //           << local_size << " x " << cols
  //           << std::endl;
  p_J.reset(new Matrix(this->communicator(), local_size, cols, Matrix::Sparse));
}

NonlinearSolverImplementation::~NonlinearSolverImplementation(void)
{
  // empty
}

// -------------------------------------------------------------
// NonlinearSolverImplementation::solve
// -------------------------------------------------------------
void 
NonlinearSolverImplementation::solve(Vector &x) 
{
  p_X.reset(&x, null_deleter());
  this->p_solve();
}

} // namespace math
} // namespace gridpack
