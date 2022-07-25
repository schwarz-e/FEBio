/*This file is part of the FEBio source code and is licensed under the MIT license
 listed below.
 
 See Copyright-FEBio.txt for details.
 
 Copyright (c) 2022 University of Utah, The Trustees of Columbia University in
 the City of New York, and others.
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.*/



#include "stdafx.h"
#include <assert.h>
#include "FEBioPolarFluid.h"
#include "FEPolarFluidSolver.h"
#include "FEPolarFluidDomain.h"
#include "FEFluidDomain.h"
#include "FEFluidResistanceBC.h"
#include "FEBackFlowStabilization.h"
#include "FEFluidNormalVelocity.h"
#include "FEFluidVelocity.h"
#include "FEFluidRotationalVelocity.h"
#include "FEPolarFluidAnalysis.h"
#include <FEBioMech/FEBodyForce.h>
#include <FEBioMech/FEContactInterface.h>
#include <FEBioMech/FEResidualVector.h>
#include <FECore/FEModel.h>
#include <FECore/log.h>
#include <FECore/DOFS.h>
#include <FECore/FEGlobalMatrix.h>
#include <FECore/sys.h>
#include <FECore/FEBoundaryCondition.h>
#include <FECore/FENodalLoad.h>
#include <FECore/FESurfaceLoad.h>
#include <FECore/FEModelLoad.h>
#include <FECore/FEAnalysis.h>
#include <FECore/FELinearConstraintManager.h>
#include <FECore/FENLConstraint.h>
#include <FECore/DumpStream.h>
#include <NumCore/NumCore.h>

//-----------------------------------------------------------------------------
// define the parameter list
BEGIN_FECORE_CLASS(FEPolarFluidSolver, FENewtonSolver)
ADD_PARAMETER(m_Vtol , "vtol"        );
ADD_PARAMETER(m_Gtol , "gtol"        );
ADD_PARAMETER(m_Ftol , "ftol"        );
ADD_PARAMETER(m_Etol , "etol"        );
ADD_PARAMETER(m_Rtol , "rtol"        );
ADD_PARAMETER(m_rhoi , "rhoi"        );
ADD_PARAMETER(m_pred , "predictor"   );
ADD_PARAMETER(m_minJf, "min_volume_ratio");
ADD_PARAMETER(m_order, "order"      );
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
//! FEPolarFluidSolver Construction
//
FEPolarFluidSolver::FEPolarFluidSolver(FEModel* pfem) : FENewtonSolver(pfem), \
m_dofW(pfem), m_dofAW(pfem), m_dofG(pfem), m_dofAG(pfem), m_dofEF(pfem)
{
    // default values
    m_Rtol = 0.001;
    m_Etol = 0.01;
    m_Vtol = 0.001;
    m_Gtol = 0.001;
    m_Ftol = 0.001;
    m_Rmin = 1.0e-20;
    m_Rmax = 0;    // not used if zero
    m_minJf = 0;
    
    m_nveq = 0;
    m_ngeq = 0;
    m_nfeq = 0;
    m_niter = 0;
    
    // assume non-symmetric
    m_msymm = REAL_UNSYMMETRIC;
    
    // default Newmark parameters for rhoi = 0
    m_rhoi = 0;
    m_alphaf = 1;
    m_alpham = 1.5;
    m_beta = 0.5625;
    m_gamma = 1;
    m_pred = 0;
    m_order = 1;
    
    // Preferred strategy is Broyden's method
    SetDefaultStrategy(QN_BROYDEN);
    
    // turn off checking for a zero diagonal
    CheckZeroDiagonal(false);
    
    // get the dof indices
    // TODO: Can this be done in Init, since there is no error checking
    if (pfem)
    {
        m_dofW.AddVariable(FEBioPolarFluid::GetVariableName(FEBioPolarFluid::RELATIVE_FLUID_VELOCITY));
        m_dofAW.AddVariable(FEBioPolarFluid::GetVariableName(FEBioPolarFluid::RELATIVE_FLUID_ACCELERATION));
        m_dofG.AddVariable(FEBioPolarFluid::GetVariableName(FEBioPolarFluid::ANGULAR_FLUID_VELOCITY));
        m_dofAG.AddVariable(FEBioPolarFluid::GetVariableName(FEBioPolarFluid::ANGULAR_FLUID_ACCELERATION));
        m_dofEF.AddVariable(FEBioPolarFluid::GetVariableName(FEBioPolarFluid::FLUID_DILATATION));
        m_dofAEF = pfem->GetDOFIndex(FEBioPolarFluid::GetVariableName(FEBioPolarFluid::FLUID_DILATATION_TDERIV), 0);
    }
}

//-----------------------------------------------------------------------------
FEPolarFluidSolver::~FEPolarFluidSolver()
{
    
}

//-----------------------------------------------------------------------------
//! Generate warnings if needed
void FEPolarFluidSolver:: SolverWarnings()
{
    // Generate warning if rigid connectors are used with symmetric stiffness
    if (m_msymm == REAL_SYMMETRIC) {
        feLogWarning("Fluid analyses require non-symmetric stiffness matrix.\nSet symmetric_stiffness flag to 0 in Control section.");
    }
}

//-----------------------------------------------------------------------------
//! Allocates and initializes the data structures used by the FEPolarFluidSolver
//
bool FEPolarFluidSolver::Init()
{
    // initialize base class
    if (FENewtonSolver::Init() == false) return false;
    
    // check parameters
    if (m_Vtol <  0.0) { feLogError("vtol must be nonnegative."); return false; }
    if (m_Gtol <  0.0) { feLogError("gtol must be nonnegative."); return false; }
    if (m_Ftol <  0.0) { feLogError("ftol must be nonnegative."); return false; }
    if (m_Etol <  0.0) { feLogError("etol must be nonnegative."); return false; }
    if (m_Rtol <  0.0) { feLogError("rtol must be nonnegative."); return false; }
    
    if (m_rhoi == -1) {
        m_alphaf = m_alpham = m_beta = m_gamma = 1.0;
    }
    else if ((m_rhoi >= 0) && (m_rhoi <= 1)) {
        m_alphaf = 1.0/(1+m_rhoi);
        if (m_order == 1)
            m_alpham = (3-m_rhoi)/(1+m_rhoi)/2; // 1st-order system
        else
            m_alpham = (2-m_rhoi)/(1+m_rhoi); // 2nd-order system
        m_beta = pow(1 + m_alpham - m_alphaf,2)/4;
        m_gamma = 0.5 + m_alpham - m_alphaf;
    }
    else { feLogError("rhoi must be -1 or between 0 and 1.\n"); return false; }
    
    // allocate vectors
    int neq = m_neq;
    m_Fn.assign(neq, 0);
    m_Fr.assign(neq, 0);
    m_Ui.assign(neq, 0);
    m_Ut.assign(neq, 0);
    m_vi.assign(m_nveq,0);
    m_Vi.assign(m_nveq,0);
    m_gi.assign(m_ngeq,0);
    m_Gi.assign(m_ngeq,0);
    m_fi.assign(m_nfeq,0);
    m_Fi.assign(m_nfeq,0);
    
    // we need to fill the total DOF vector m_Ut
    // TODO: I need to find an easier way to do this
    FEModel& fem = *GetFEModel();
    FEMesh& mesh = fem.GetMesh();
    gather(m_Ut, mesh, m_dofW[0]);
    gather(m_Ut, mesh, m_dofW[1]);
    gather(m_Ut, mesh, m_dofW[2]);
    gather(m_Ut, mesh, m_dofG[0]);
    gather(m_Ut, mesh, m_dofG[1]);
    gather(m_Ut, mesh, m_dofG[2]);
    gather(m_Ut, mesh, m_dofEF[0]);
    
    
    // set flag for transient or steady-state analyses
    FEAnalysis* pstep = fem.GetCurrentStep();
    for (int i=0; i<mesh.Domains(); ++i)
    {
        FEDomain& dom = mesh.Domain(i);
        FEFluidDomain* fdom = dynamic_cast<FEFluidDomain*>(&dom);
        FEPolarFluidDomain* pfdom = dynamic_cast<FEPolarFluidDomain*>(&dom);
        if (fdom) {
            if (pstep->m_nanalysis == FEPolarFluidAnalysis::STEADY_STATE)
                fdom->SetSteadyStateAnalysis();
            else
                fdom->SetTransientAnalysis();
        }
        else if (pfdom) {
            if (pstep->m_nanalysis == FEPolarFluidAnalysis::STEADY_STATE)
                pfdom->SetSteadyStateAnalysis();
            else
                pfdom->SetTransientAnalysis();
        }
    }
    
    SolverWarnings();
    
    return true;
}

//-----------------------------------------------------------------------------
//! Initialize equations
bool FEPolarFluidSolver::InitEquations()
{
    // Add the solution variables
    AddSolutionVariable(&m_dofW , 1, "velocity"        , m_Vtol);
    AddSolutionVariable(&m_dofG , 1, "angular velocity", m_Gtol);
    AddSolutionVariable(&m_dofEF, 1, "dilatation"      , m_Ftol);
    
    // base class initialization
    if (FENewtonSolver::InitEquations() == false) return false;

    // determine the number of velocity and dilatation equations
    FEMesh& mesh = GetFEModel()->GetMesh();
    m_nveq = m_ngeq = m_nfeq = 0;
    
    for (int i=0; i<mesh.Nodes(); ++i)
    {
        FENode& n = mesh.Node(i);
        if (n.m_ID[m_dofW[0] ] != -1) m_nveq++;
        if (n.m_ID[m_dofW[1] ] != -1) m_nveq++;
        if (n.m_ID[m_dofW[2] ] != -1) m_nveq++;
        if (n.m_ID[m_dofG[0] ] != -1) m_ngeq++;
        if (n.m_ID[m_dofG[1] ] != -1) m_ngeq++;
        if (n.m_ID[m_dofG[2] ] != -1) m_ngeq++;
        if (n.m_ID[m_dofEF[0]] != -1) m_nfeq++;
    }
    
    // Next, we add any Lagrange Multipliers
    FEModel& fem = *GetFEModel();
    for (int i = 0; i < fem.NonlinearConstraints(); ++i)
    {
        FENLConstraint* lmc = fem.NonlinearConstraint(i);
        if (lmc->IsActive())
        {
            m_neq += lmc->InitEquations(m_neq);
        }
    }
    for (int i = 0; i < fem.SurfacePairConstraints(); ++i)
    {
        FESurfacePairConstraint* spc = fem.SurfacePairConstraint(i);
        if (spc->IsActive())
        {
            m_neq += spc->InitEquations(m_neq);
        }
    }
    
    // All initialization is done
    return true;
}

//-----------------------------------------------------------------------------
void FEPolarFluidSolver::GetVelocityData(vector<double> &vi, vector<double> &ui)
{
    FEModel& fem = *GetFEModel();
    
    int N = fem.GetMesh().Nodes(), nid, m = 0;
    zero(vi);
    for (int i=0; i<N; ++i)
    {
        FENode& n = fem.GetMesh().Node(i);
        nid = n.m_ID[m_dofW[0]];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            vi[m++] = ui[nid];
            assert(m <= (int) vi.size());
        }
        nid = n.m_ID[m_dofW[1]];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            vi[m++] = ui[nid];
            assert(m <= (int) vi.size());
        }
        nid = n.m_ID[m_dofW[2]];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            vi[m++] = ui[nid];
            assert(m <= (int) vi.size());
        }
    }
}

//-----------------------------------------------------------------------------
void FEPolarFluidSolver::GetAngularVelocityData(vector<double> &xi, vector<double> &ui)
{
    FEModel& fem = *GetFEModel();
    
    int N = fem.GetMesh().Nodes(), nid, m = 0;
    zero(xi);
    for (int i=0; i<N; ++i)
    {
        FENode& n = fem.GetMesh().Node(i);
        nid = n.m_ID[m_dofG[0]];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            xi[m++] = ui[nid];
            assert(m <= (int) xi.size());
        }
        nid = n.m_ID[m_dofG[1]];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            xi[m++] = ui[nid];
            assert(m <= (int) xi.size());
        }
        nid = n.m_ID[m_dofG[2]];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            xi[m++] = ui[nid];
            assert(m <= (int) xi.size());
        }
    }
}

//-----------------------------------------------------------------------------
void FEPolarFluidSolver::GetDilatationData(vector<double> &ei, vector<double> &ui)
{
    FEModel& fem = *GetFEModel();
    
    int N = fem.GetMesh().Nodes(), nid, m = 0;
    zero(ei);
    for (int i=0; i<N; ++i)
    {
        FENode& n = fem.GetMesh().Node(i);
        nid = n.m_ID[m_dofEF[0]];
        if (nid != -1)
        {
            nid = (nid < -1 ? -nid-2 : nid);
            ei[m++] = ui[nid];
            assert(m <= (int) ei.size());
        }
    }
}

//-----------------------------------------------------------------------------
//! Save data to dump file

void FEPolarFluidSolver::Serialize(DumpStream& ar)
{
    // Serialize parameters
    FENewtonSolver::Serialize(ar);
    
    ar & m_nveq;
    ar & m_ngeq;
    ar & m_nfeq;

    ar & m_rhoi & m_alphaf & m_alpham;
    ar & m_beta & m_gamma;
    ar & m_pred;
    
    ar & m_Fn & m_Ui & m_Ut & m_Fr;
    ar & m_vi & m_Vi;
    ar & m_gi & m_Gi;
    ar & m_fi & m_Fi;
    
}

//-----------------------------------------------------------------------------
//! Update the kinematics of the model, such as nodal positions, velocities,
//! accelerations, etc.
void FEPolarFluidSolver::UpdateKinematics(vector<double>& ui)
{
    FEModel& fem = *GetFEModel();
    
    // get the mesh
    FEMesh& mesh = fem.GetMesh();
    
    // update nodes
    vector<double> U(m_Ut.size());
    for (size_t i=0; i<m_Ut.size(); ++i) U[i] = ui[i] + m_Ui[i] + m_Ut[i];
    
    scatter(U, mesh, m_dofW[0]);
    scatter(U, mesh, m_dofW[1]);
    scatter(U, mesh, m_dofW[2]);
    scatter(U, mesh, m_dofG[0]);
    scatter(U, mesh, m_dofG[1]);
    scatter(U, mesh, m_dofG[2]);
    scatter(U, mesh, m_dofEF[0]);
    
    // force dilatations to remain greater than -1
    if (m_minJf > 0) {
        const int NN = mesh.Nodes();
        for (int i=0; i<NN; ++i)
        {
            FENode& node = mesh.Node(i);
            if (node.get(m_dofEF[0]) <= -1.0)
                node.set(m_dofEF[0], m_minJf - 1.0);
        }
    }
    
    // make sure the prescribed BCs are fullfilled
    int nvel = fem.BoundaryConditions();
    for (int i=0; i<nvel; ++i)
    {
        FEBoundaryCondition& bc = *fem.BoundaryCondition(i);
        if (bc.IsActive()) bc.Update();
    }
    // enforce the linear constraints
    // TODO: do we really have to do this? Shouldn't the algorithm
    // already guarantee that the linear constraints are satisfied?
    FELinearConstraintManager& LCM = fem.GetLinearConstraintManager();
    if (LCM.LinearConstraints() > 0)
    {
        LCM.Update();
    }
    
    // update time derivatives of velocity and dilatation
    // for dynamic simulations
    FEAnalysis* pstep = fem.GetCurrentStep();
    if (pstep->m_nanalysis == FEPolarFluidAnalysis::DYNAMIC)
    {
        int N = mesh.Nodes();
        double dt = fem.GetTime().timeIncrement;
        double cgi = 1 - 1.0/m_gamma;
        for (int i=0; i<N; ++i)
        {
            FENode& n = mesh.Node(i);
            
            // relative fluid velocity material time derivative
            vec3d wt = n.get_vec3d(m_dofW[0], m_dofW[1], m_dofW[2]);
            vec3d wp = n.get_vec3d_prev(m_dofW[0], m_dofW[1], m_dofW[2]);
            vec3d awt = n.get_vec3d(m_dofAW[0], m_dofAW[1], m_dofAW[2]);
            vec3d awp = n.get_vec3d_prev(m_dofAW[0], m_dofAW[1], m_dofAW[2]);
            awt = awp*cgi + (wt - wp)/(m_gamma*dt);
            n.set_vec3d(m_dofAW[0], m_dofAW[1], m_dofAW[2], awt);
            
            // angular fluid velocity material time derivative
            vec3d gt = n.get_vec3d(m_dofG[0], m_dofG[1], m_dofG[2]);
            vec3d gp = n.get_vec3d_prev(m_dofG[0], m_dofG[1], m_dofG[2]);
            vec3d agt = n.get_vec3d(m_dofAG[0], m_dofAG[1], m_dofAG[2]);
            vec3d agp = n.get_vec3d_prev(m_dofAG[0], m_dofAG[1], m_dofAG[2]);
            agt = agp*cgi + (gt - gp)/(m_gamma*dt);
            n.set_vec3d(m_dofAG[0], m_dofAG[1], m_dofAG[2], agt);
            
            // dilatation time derivative
            double eft = n.get(m_dofEF[0]);
            double efp = n.get_prev(m_dofEF[0]);
            double aefp = n.get_prev(m_dofAEF);
            double aeft = aefp*cgi + (eft - efp)/(m_gamma*dt);
            n.set(m_dofAEF, aeft);
        }
    }
}

//-----------------------------------------------------------------------------
//! Update DOF increments
void FEPolarFluidSolver::UpdateIncrements(vector<double>& Ui, vector<double>& ui, bool emap)
{
    FEModel& fem = *GetFEModel();
    
    // get the mesh
    FEMesh& mesh = fem.GetMesh();
    
    // update flexible nodes
    int n;
    for (int i=0; i<mesh.Nodes(); ++i)
    {
        FENode& node = mesh.Node(i);
        
        // fluid relative velocity
        if ((n = node.m_ID[m_dofW[0]]) >= 0) Ui[n] += ui[n];
        if ((n = node.m_ID[m_dofW[1]]) >= 0) Ui[n] += ui[n];
        if ((n = node.m_ID[m_dofW[2]]) >= 0) Ui[n] += ui[n];
        
        // fluid angular velocity
        if ((n = node.m_ID[m_dofG[0]]) >= 0) Ui[n] += ui[n];
        if ((n = node.m_ID[m_dofG[1]]) >= 0) Ui[n] += ui[n];
        if ((n = node.m_ID[m_dofG[2]]) >= 0) Ui[n] += ui[n];
        
        // fluid dilatation
        if ((n = node.m_ID[m_dofEF[0]]) >= 0) Ui[n] += ui[n];
    }
}

//-----------------------------------------------------------------------------
//! Updates the current state of the model
void FEPolarFluidSolver::Update(vector<double>& ui)
{
    FEModel& fem = *GetFEModel();
    FETimeInfo& tp = fem.GetTime();
    tp.currentIteration = m_niter;
    
    // update kinematics
    UpdateKinematics(ui);
    
    // update constraints
    if (fem.NonlinearConstraints() > 0) UpdateConstraints();
    
    // update model state
    GetFEModel()->Update();
}

//-----------------------------------------------------------------------------
//! Update nonlinear constraints
void FEPolarFluidSolver::UpdateConstraints()
{
    FEModel& fem = *GetFEModel();
    FETimeInfo& tp = fem.GetTime();
    tp.currentIteration = m_niter;
    
    // Update all nonlinear constraints
    for (int i = 0; i<fem.NonlinearConstraints(); ++i)
    {
        FENLConstraint* pci = fem.NonlinearConstraint(i);
        if (pci->IsActive()) pci->Update();
    }
}

//-----------------------------------------------------------------------------
bool FEPolarFluidSolver::InitStep(double time)
{
    FEModel& fem = *GetFEModel();
    
    // get time integration parameters
    FETimeInfo& tp = fem.GetTime();
    tp.alpha = m_alphaf;
    tp.beta  = m_beta;
    tp.gamma = m_gamma;
    tp.alphaf = m_alphaf;
    tp.alpham = m_alpham;
    
    // evaluate load curve values at current (or intermediate) time
    double t = tp.currentTime;
    //    double dt = tp.timeIncrement;
    //    double ta = (t > 0) ? t - (1-m_alpha)*dt : m_alpha*dt;
    //    return FESolver::InitStep(ta);
    return FESolver::InitStep(t);
}

//-----------------------------------------------------------------------------
//! Prepares the data for the first BFGS-iteration.
void FEPolarFluidSolver::PrepStep()
{
    FEModel& fem = *GetFEModel();
    
    FETimeInfo& tp = fem.GetTime();
    double dt = tp.timeIncrement;
    tp.currentIteration = m_niter;
    
    // zero total DOFs
    zero(m_Ui);
    zero(m_Vi);
    zero(m_Gi);
    zero(m_Fi);
    
    // store previous mesh state
    // we need them for strain and acceleration calculations
    FEMesh& mesh = fem.GetMesh();
    for (int i=0; i<mesh.Nodes(); ++i)
    {
        FENode& ni = mesh.Node(i);
        ni.m_rp = ni.m_rt;
        ni.m_dp = ni.m_dt = ni.m_d0;
        ni.UpdateValues();
        
        switch (m_pred) {
            case 0:
            {
                // initial guess at start of new time step (default)
                vec3d awp = ni.get_vec3d_prev(m_dofAW[0], m_dofAW[1], m_dofAW[2]);
                ni.set_vec3d(m_dofAW[0], m_dofAW[1], m_dofAW[2], awp*(m_gamma-1)/m_gamma);
                
                vec3d agp = ni.get_vec3d_prev(m_dofAG[0], m_dofAG[1], m_dofAG[2]);
                ni.set_vec3d(m_dofAG[0], m_dofAG[1], m_dofAG[2], agp*(m_gamma-1)/m_gamma);
                
                ni.set(m_dofAEF, ni.get_prev(m_dofAEF)*(m_gamma-1)/m_gamma);
            }
                break;
                
            case 1:
            {
                // initial guess at start of new time step (Zero Ydot)
                vec3d wp = ni.get_vec3d_prev(m_dofW[0], m_dofW[1], m_dofW[2]);
                vec3d awp = ni.get_vec3d_prev(m_dofAW[0], m_dofAW[1], m_dofAW[2]);
                vec3d gp = ni.get_vec3d_prev(m_dofG[0], m_dofG[1], m_dofG[2]);
                vec3d agp = ni.get_vec3d_prev(m_dofAG[0], m_dofAG[1], m_dofAG[2]);
                ni.set_vec3d(m_dofW[0], m_dofW[1], m_dofW[2], wp + awp*dt*(1-m_gamma)*m_alphaf);
                ni.set_vec3d(m_dofG[0], m_dofG[1], m_dofG[2], gp + agp*dt*(1-m_gamma)*m_alphaf);
                ni.set(m_dofEF[0], ni.get_prev(m_dofEF[0]) + ni.get_prev(m_dofAEF)*dt*(1-m_gamma)*m_alphaf);
            }
                break;
                
            case 2:
            {
                // initial guess at start of new time step (Same Ydot)
                vec3d awp = ni.get_vec3d_prev(m_dofAW[0], m_dofAW[1], m_dofAW[2]);
                ni.set_vec3d(m_dofAW[0], m_dofAW[1], m_dofAW[2], awp);
                vec3d agp = ni.get_vec3d_prev(m_dofAG[0], m_dofAG[1], m_dofAG[2]);
                ni.set_vec3d(m_dofAG[0], m_dofAG[1], m_dofAG[2], agp);
                ni.set(m_dofAEF, ni.get_prev(m_dofAEF));
                
                vec3d wp = ni.get_vec3d_prev(m_dofW[0], m_dofW[1], m_dofW[2]);
                ni.set_vec3d(m_dofW[0], m_dofW[1], m_dofW[2], wp + awp*dt);
                vec3d gp = ni.get_vec3d_prev(m_dofG[0], m_dofG[1], m_dofG[2]);
                ni.set_vec3d(m_dofG[0], m_dofG[1], m_dofG[2], gp + agp*dt);
                ni.set(m_dofEF[0], ni.get_prev(m_dofEF[0]) + ni.get_prev(m_dofAEF)*dt);
            }
                break;
                
            default:
                break;
        }
    }
    
    // apply prescribed velocities
    // we save the prescribed velocity increments in the ui vector
    vector<double>& ui = m_ui;
    zero(ui);
    int nbc = fem.BoundaryConditions();
    for (int i=0; i<nbc; ++i)
    {
        FEBoundaryCondition& bc = *fem.BoundaryCondition(i);
        if (bc.IsActive()) bc.PrepStep(ui);
    }
    
    // apply prescribed DOFs for specialized surface loads
    int nsl = fem.ModelLoads();
    for (int i = 0; i < nsl; ++i)
    {
        FEModelLoad& pml = *fem.ModelLoad(i);
        if (pml.IsActive() && HasActiveDofs(pml.GetDofList())) pml.Update();
    }

    // do the linear constraints
    fem.GetLinearConstraintManager().PrepStep();
    
    // initialize material point data
    // NOTE: do this before the stresses are updated
    // TODO: does it matter if the stresses are updated before
    //       the material point data is initialized
    // update domain data
    for (int i=0; i<mesh.Domains(); ++i) mesh.Domain(i).PreSolveUpdate(tp);

    // update stresses
    fem.Update();
    
    // see if we need to do contact augmentations
    m_baugment = false;
    for (int i = 0; i<fem.SurfacePairConstraints(); ++i)
    {
        FEContactInterface& ci = dynamic_cast<FEContactInterface&>(*fem.SurfacePairConstraint(i));
        if (ci.IsActive() && (ci.m_laugon != 1)) m_baugment = true;
    }
    
    // see if we have to do nonlinear constraint augmentations
    if (fem.NonlinearConstraints() != 0) m_baugment = true;
}

//-----------------------------------------------------------------------------
bool FEPolarFluidSolver::Quasin()
{
    FEModel& fem = *GetFEModel();
    
    vector<double> u0(m_neq);
    vector<double> Rold(m_neq);
    
    // convergence norms
    double    normR1;       // residual norm
    double    normE1;       // energy norm
    double    normV;        // velocity norm
    double    normv;        // velocity increment norm
    double    normG;        // angular velocity norm
    double    normg;        // angular velocity increment norm
    double    normRi = 0;   // initial residual norm
    double    normVi = 0;   // initial velocity norm
    double    normGi = 0;   // initial angular velocity norm
    double    normEi = 0;   // initial energy norm
    double    normEm = 0;   // max energy norm
    double    normFi = 0;   // initial dilatation norm
    double    normF;        // current dilatation norm
    double    normf;        // incremement dilatation norm
    
    // prepare for the first iteration
    const FETimeInfo& tp = fem.GetTime();
    PrepStep();
    
    // init QN method
    if (QNInit() == false) return false;
    
    // loop until converged or when max nr of reformations reached
    bool bconv = false;        // convergence flag
    do
    {
        feLog(" %d\n", m_niter+1);
        
        // assume we'll converge.
        bconv = true;
        
        // solve the equations (returns line search; solution stored in m_ui)
        double s = QNSolve();
        
        // extract the velocity and dilatation increments
        GetVelocityData(m_vi, m_ui);
        GetAngularVelocityData(m_gi, m_ui);
        GetDilatationData(m_fi, m_ui);
        
        // set initial convergence norms
        if (m_niter == 0)
        {
            normRi = fabs(m_R0*m_R0);
            normEi = fabs(m_ui*m_R0);
            normVi = fabs(m_vi*m_vi);
            normGi = fabs(m_gi*m_gi);
            normFi = fabs(m_fi*m_fi);
            normEm = normEi;
        }
        
        // calculate norms
        // update all degrees of freedom
        for (int i=0; i<m_neq; ++i) m_Ui[i] += s*m_ui[i];
        
        // update velocities
        for (int i = 0; i<m_nveq; ++i) m_Vi[i] += s*m_vi[i];
        
        // update angular velocities
        for (int i = 0; i<m_ngeq; ++i) m_Gi[i] += s*m_gi[i];
        
        // update dilatations
        for (int i = 0; i<m_nfeq; ++i) m_Fi[i] += s*m_fi[i];
        
        // calculate the norms
        normR1 = m_R1*m_R1;
        normv  = (m_vi*m_vi)*(s*s);
        normV  = m_Vi*m_Vi;
        normg  = (m_gi*m_gi)*(s*s);
        normG  = m_Gi*m_Gi;
        normf  = (m_fi*m_fi)*(s*s);
        normF  = m_Fi*m_Fi;
        normE1 = s*fabs(m_ui*m_R1);
        
        // check for nans
        if (ISNAN(normR1)) throw NANDetected();
        
        // check residual norm
        if ((m_Rtol > 0) && (normR1 > m_Rtol*normRi)) bconv = false;
        
        // check velocity norm
        if ((m_Vtol > 0) && (normv  > (m_Vtol*m_Vtol)*normV )) bconv = false;
        
        // check angular velocity norm
        if ((m_Gtol > 0) && (normg  > (m_Gtol*m_Gtol)*normG )) bconv = false;
        
        // check dilatation norm
        if ((m_Ftol > 0) && (normf  > (m_Ftol*m_Ftol)*normF )) bconv = false;
        
        // check energy norm
        if ((m_Etol > 0) && (normE1 > m_Etol*normEi)) bconv = false;
        
        // check linestep size
        if ((m_lineSearch->m_LStol > 0) && (s < m_lineSearch->m_LSmin)) bconv = false;
        
        // check energy divergence
        if (normE1 > normEm) bconv = false;
        
        // print convergence summary
        feLog(" Nonlinear solution status: time= %lg\n", tp.currentTime);
        feLog("\tstiffness updates             = %d\n", m_qnstrategy->m_nups);
        feLog("\tright hand side evaluations   = %d\n", m_nrhs);
        feLog("\tstiffness matrix reformations = %d\n", m_nref);
        if (m_lineSearch->m_LStol > 0) feLog("\tstep from line search         = %lf\n", s);
        feLog("\tconvergence norms :     INITIAL         CURRENT         REQUIRED\n");
        feLog("\t   residual         %15le %15le %15le \n", normRi, normR1, m_Rtol*normRi);
        feLog("\t   energy           %15le %15le %15le \n", normEi, normE1, m_Etol*normEi);
        feLog("\t   velocity         %15le %15le %15le \n", normVi, normv ,(m_Vtol*m_Vtol)*normV );
        feLog("\t   angular velocity %15le %15le %15le \n", normGi, normg ,(m_Gtol*m_Gtol)*normG );
        feLog("\t   dilatation       %15le %15le %15le \n", normFi, normf ,(m_Ftol*m_Ftol)*normF );
        
        // see if we may have a small residual
        if ((bconv == false) && (normR1 < m_Rmin))
        {
            // check for almost zero-residual on the first iteration
            // this might be an indication that there is no force on the system
            feLogWarning("No force acting on the system.");
            bconv = true;
        }
        
        // see if we have exceeded the max residual
        if ((bconv == false) && (m_Rmax > 0) && (normR1 >= m_Rmax))
        {
            // doesn't look like we're getting anywhere, so let's retry the time step
            throw MaxResidualError();
        }
        
        // check if we have converged.
        // If not, calculate the BFGS update vectors
        if (bconv == false)
        {
            if (s < m_lineSearch->m_LSmin)
            {
                // check for zero linestep size
                feLogWarning("Zero linestep size. Stiffness matrix will now be reformed");
                QNForceReform(true);
            }
            else if ((normE1 > normEm) && m_bdivreform)
            {
                // check for diverging
                feLogWarning("Problem is diverging. Stiffness matrix will now be reformed");
                normEm = normE1;
                normEi = normE1;
                normRi = normR1;
                normVi = normv;
                normGi = normg;
                normFi = normf;
                QNForceReform(true);
            }
            
            // Do the QN update (This may also do a stiffness reformation if necessary)
            bool bret = QNUpdate();
            
            // something went wrong with the update, so we'll need to break
            if (bret == false) break;
        }
        else if (m_baugment)
        {
            // Do augmentations
            bconv = DoAugmentations();
        }
        
        // increase iteration number
        m_niter++;
        
        // do minor iterations callbacks
        fem.DoCallback(CB_MINOR_ITERS);
    }
    while (bconv == false);
    
    // if converged we update the total velocities
    if (bconv) UpdateIncrements(m_Ut, m_Ui, true);
    
    return bconv;
}

//-----------------------------------------------------------------------------
//! Calculates global stiffness matrix.

bool FEPolarFluidSolver::StiffnessMatrix(FELinearSystem& LS)
{
    FEModel& fem = *GetFEModel();
    
    const FETimeInfo& tp = fem.GetTime();
    
    // get the mesh
    FEMesh& mesh = fem.GetMesh();
    
    // calculate the stiffness matrix for each domain
    for (int i=0; i<mesh.Domains(); ++i)
    {
        FEDomain& dom = mesh.Domain(i);
        if (dom.IsActive()) {
            FEFluidDomain* fdom = dynamic_cast<FEFluidDomain*>(&dom);
            FEPolarFluidDomain* pfdom = dynamic_cast<FEPolarFluidDomain*>(&dom);
            if (fdom) fdom->StiffnessMatrix(LS, tp);
            else if (pfdom) pfdom->StiffnessMatrix(LS, tp);
        }
    }
    
    // calculate the body force stiffness matrix for each domain
    // but not for solid domains (since they have no mass in FSI)
    int NML = fem.ModelLoads();
    for (int j = 0; j<NML; ++j)
    {
        FEModelLoad* pml = fem.ModelLoad(j);
        FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(pml);
        if (pbf && pbf->IsActive())
        {
            for (int i = 0; i<pbf->Domains(); ++i)
            {
                FEDomain* dom = pbf->Domain(i);
                if (dom->IsActive())
                {
                    FEFluidDomain* fdom = dynamic_cast<FEFluidDomain*>(dom);
                    FEPolarFluidDomain* pfdom = dynamic_cast<FEPolarFluidDomain*>(dom);
                    if (fdom) fdom->BodyForceStiffness(LS, tp, *pbf);
                    else if (pfdom) pfdom->BodyForceStiffness(LS, tp, *pbf);
                }
            }
        }
    }
    
    // TODO: add body force stiffness for rigid bodies
    
    // calculate contact stiffness
    ContactStiffness(LS);
    
    // calculate stiffness matrix due to model loads
    int nsl = fem.ModelLoads();
    for (int i=0; i<nsl; ++i)
    {
        FEModelLoad* pml = fem.ModelLoad(i);
        if (pml->IsActive()) pml->StiffnessMatrix(LS);
    }
    
    // Add mass matrix
    // loop over all domains (except rigid)
    for (int i = 0; i<mesh.Domains(); ++i)
    {
        FEDomain& dom = mesh.Domain(i);
        if (dom.IsActive())
        {
            FEFluidDomain* fdom = dynamic_cast<FEFluidDomain*>(&dom);
            FEPolarFluidDomain* pfdom = dynamic_cast<FEPolarFluidDomain*>(&dom);
            if (fdom) fdom->MassMatrix(LS, tp);
            else if (pfdom) pfdom->MassMatrix(LS, tp);
        }
    }
    
    // calculate nonlinear constraint stiffness
    // note that this is the contribution of the
    // constrainst enforced with augmented lagrangian
    NonLinearConstraintStiffness(LS, tp);
    
    return true;
}

//-----------------------------------------------------------------------------
//! Calculate the stiffness contribution due to nonlinear constraints
void FEPolarFluidSolver::NonLinearConstraintStiffness(FELinearSystem& LS, const FETimeInfo& tp)
{
    FEModel& fem = *GetFEModel();
    
    int N = fem.NonlinearConstraints();
    for (int i=0; i<N; ++i)
    {
        FENLConstraint* plc = fem.NonlinearConstraint(i);
        if (plc->IsActive()) plc->StiffnessMatrix(LS, tp);
    }
}

//-----------------------------------------------------------------------------
//! This function calculates the contact stiffness matrix

void FEPolarFluidSolver::ContactStiffness(FELinearSystem& LS)
{
    FEModel& fem = *GetFEModel();
    
    const FETimeInfo& tp = fem.GetTime();
    for (int i = 0; i<fem.SurfacePairConstraints(); ++i)
    {
        FEContactInterface* pci = dynamic_cast<FEContactInterface*>(fem.SurfacePairConstraint(i));
        if (pci->IsActive()) pci->StiffnessMatrix(LS, tp);
    }
}

//-----------------------------------------------------------------------------
//! Calculates the contact forces
void FEPolarFluidSolver::ContactForces(FEGlobalVector& R)
{
    FEModel& fem = *GetFEModel();
    
    const FETimeInfo& tp = fem.GetTime();
    for (int i = 0; i<fem.SurfacePairConstraints(); ++i)
    {
        FEContactInterface* pci = dynamic_cast<FEContactInterface*>(fem.SurfacePairConstraint(i));
        if (pci->IsActive()) pci->LoadVector(R, tp);
    }
}

//-----------------------------------------------------------------------------
//! calculates the residual vector
//! Note that the concentrated nodal forces are not calculated here.
//! This is because they do not depend on the geometry
//! so we only calculate them once (in Quasin) and then add them here.

bool FEPolarFluidSolver::Residual(vector<double>& R)
{
    FEModel& fem = *GetFEModel();
    
    // get the time information
    const FETimeInfo& tp = fem.GetTime();
    
    // initialize residual with concentrated nodal loads
    R = m_Fn;
    
    // zero nodal reaction forces
    zero(m_Fr);
    
    // setup the global vector
    FEResidualVector RHS(fem, R, m_Fr);
    
    // get the mesh
    FEMesh& mesh = fem.GetMesh();
    
    // calculate the internal (stress) forces
    for (int i=0; i<mesh.Domains(); ++i)
    {
        FEDomain& dom = mesh.Domain(i);
        if (dom.IsActive())
        {
            FEFluidDomain* fdom = dynamic_cast<FEFluidDomain*>(&dom);
            FEPolarFluidDomain* pfdom = dynamic_cast<FEPolarFluidDomain*>(&dom);
            if (fdom) fdom->InternalForces(RHS, tp);
            else if (pfdom) pfdom->InternalForces(RHS, tp);
        }
    }
    
    // calculate the body forces
    for (int j = 0; j<fem.ModelLoads(); ++j)
    {
        FEModelLoad* pml = fem.ModelLoad(j);
        FEBodyForce* pbf = dynamic_cast<FEBodyForce*>(pml);
        if (pbf && pbf->IsActive())
        {
            for (int i = 0; i<pbf->Domains(); ++i)
            {
                FEDomain* dom = pbf->Domain(i);
                if (dom->IsActive())
                {
                    FEFluidDomain* fdom = dynamic_cast<FEFluidDomain*>(dom);
                    FEPolarFluidDomain* pfdom = dynamic_cast<FEPolarFluidDomain*>(dom);
                    if (fdom) fdom->BodyForce(RHS, tp, *pbf);
                    else if (pfdom) pfdom->BodyForce(RHS, tp, *pbf);
                }
            }
        }
    }
    
    // calculate inertial forces
    for (int i=0; i<mesh.Domains(); ++i)
    {
        FEDomain& dom = mesh.Domain(i);
        if (dom.IsActive())
        {
            FEFluidDomain* fdom = dynamic_cast<FEFluidDomain*>(&dom);
            FEPolarFluidDomain* pfdom = dynamic_cast<FEPolarFluidDomain*>(&dom);
            if (fdom) fdom->InertialForces(RHS, tp);
            else if (pfdom) pfdom->InertialForces(RHS, tp);
        }
    }
    
    // calculate contact forces
    ContactForces(RHS);
    
    // calculate nonlinear constraint forces
    // note that these are the linear constraints
    // enforced using the augmented lagrangian
    NonLinearConstraintForces(RHS, tp);
    
    // add model loads
    int NML = fem.ModelLoads();
    for (int i=0; i<NML; ++i)
    {
        FEModelLoad& mli = *fem.ModelLoad(i);
        if (mli.IsActive())
        {
            mli.LoadVector(RHS);
        }
    }
    
    // set the nodal reaction forces
    // TODO: Is this a good place to do this?
    for (int i=0; i<mesh.Nodes(); ++i)
    {
        FENode& node = mesh.Node(i);
        node.set_load(m_dofW[0], 0);
        node.set_load(m_dofW[1], 0);
        node.set_load(m_dofW[2], 0);
        
        int n;
        if ((n = -node.m_ID[m_dofW[0]] - 2) >= 0) node.set_load(m_dofW[0], -m_Fr[n]);
        if ((n = -node.m_ID[m_dofW[1]] - 2) >= 0) node.set_load(m_dofW[1], -m_Fr[n]);
        if ((n = -node.m_ID[m_dofW[2]] - 2) >= 0) node.set_load(m_dofW[2], -m_Fr[n]);
    }
    
    // increase RHS counter
    m_nrhs++;
    
    return true;
}

//-----------------------------------------------------------------------------
//! calculate the nonlinear constraint forces
void FEPolarFluidSolver::NonLinearConstraintForces(FEGlobalVector& R, const FETimeInfo& tp)
{
    FEModel& fem = *GetFEModel();
    int N = fem.NonlinearConstraints();
    for (int i=0; i<N; ++i)
    {
        FENLConstraint* plc = fem.NonlinearConstraint(i);
        if (plc->IsActive()) plc->LoadVector(R, tp);
    }
}