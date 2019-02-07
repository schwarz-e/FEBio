#pragma once
#include <FECore/LinearSolver.h>
#include <FECore/SparseMatrix.h>
#include <FECore/Preconditioner.h>

//-----------------------------------------------------------------------------
//! This class implements an interface to the MKL FGMRES iterative solver for 
//! nonsymmetric indefinite matrices (without pre-conditioning).
class FGMRESSolver : public IterativeLinearSolver
{
public:
	//! constructor
	FGMRESSolver(FEModel* fem);

	//! do any pre-processing (allocates temp storage)
	bool PreProcess() override;

	//! Factor the matrix
	bool Factor() override;

	//! Calculate the solution of RHS b and store solution in x
	bool BackSolve(double* x, double* b) override;

	//! Clean up
	void Destroy() override;

	//! Return a sparse matrix compatible with this solver
	SparseMatrix* CreateSparseMatrix(Matrix_Type ntype) override;

	//! Set the sparse matrix
	bool SetSparseMatrix(SparseMatrix* pA) override;

	//! Set max nr of iterations
	void SetMaxIterations(int n);

	//! Get the max nr of iterations
	int GetMaxIterations() const;

	//! Set the nr of non-restarted iterations
	void SetNonRestartedIterations(int n);

	// Set the print level
	void SetPrintLevel(int n) override;

	// set residual stopping test flag
	void DoResidualStoppingTest(bool b);

	// set zero norm stopping test flag
	void DoZeroNormStoppingTest(bool b);

	// set the relative convergence tolerance for the residual stopping test
	void SetRelativeResidualTolerance(double tol);

	// set the absolute convergence tolerance for the residual stopping test
	void SetAbsoluteResidualTolerance(double tol);

	//! This solver does not use a preconditioner
	bool HasPreconditioner() const override;

	//! convenience function for solving linear system Ax = b
	bool Solve(SparseMatrix* A, vector<double>& x, vector<double>& b);

	//! fail if max iterations reached
	void FailOnMaxIterations(bool b);

	//! print the condition number
	void PrintConditionNumber(bool b);

public:
	// set the preconditioner
	void SetPreconditioner(Preconditioner* P) override;

	// get the preconditioner
	Preconditioner* GetPreconditioner() override;

	//! Set the right preconditioner
	void SetRightPreconditioner(Preconditioner* R);

protected:
	virtual void mult_vector(double* x, double* y);

protected:
	SparseMatrix* GetSparseMatrix() { return m_pA; }

private:
	int		m_maxiter;			// max nr of iterations
	int		m_nrestart;			// max nr of non-restarted iterations
	int		m_print_level;		// output level
	bool	m_doResidualTest;	// do the residual stopping test
	bool	m_doZeroNormTest;	// do the zero-norm stopping test
	double	m_reltol;			// relative residual convergence tolerance
	double	m_abstol;			// absolute residual tolerance
	bool	m_maxIterFail;
	bool	m_print_cn;			// Calculate and print the condition number

private:
	SparseMatrix*	m_pA;		//!< the sparse matrix format
	Preconditioner*	m_P;		//!< the left preconditioner
	Preconditioner*	m_R;		//!< the right preconditioner
	vector<double>	m_tmp;
	vector<double>	m_Rv;		//!< used when a right preconditioner is ued
};
