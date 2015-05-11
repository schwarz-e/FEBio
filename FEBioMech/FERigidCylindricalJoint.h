//
//  FERigidCylindricalJoint.h
//  FEBioMech
//
//  Created by Gerard Ateshian on 5/9/15.
//  Copyright (c) 2015 febio.org. All rights reserved.
//

#ifndef __FEBioMech__FERigidCylindricalJoint__
#define __FEBioMech__FERigidCylindricalJoint__

#include "FECore/vec3d.h"
#include "FECore/DumpFile.h"
#include "FECore/FENLConstraint.h"
#include "FECore/LoadCurve.h"

//-----------------------------------------------------------------------------
//! The FERigidCylindricalJoint class implements a cylindrical joint. The rigid joint
//! allows the user to connect two rigid bodies at a point in space
//! and allow rotation about, and translation along, a single prescribed axis.

class FERigidCylindricalJoint : public FENLConstraint
{
public:
    //! constructor
    FERigidCylindricalJoint(FEModel* pfem);
    
    //! destructor
    virtual ~FERigidCylindricalJoint() {}
    
    //! initialization
    bool Init();
    
    //! calculates the joint forces
    void Residual(FEGlobalVector& R);
    
    //! calculates the joint stiffness
    void StiffnessMatrix(FESolver* psolver);
    
    //! calculate Lagrangian augmentation
    bool Augment(int naug);
    
    //! serialize data to archive
    void Serialize(DumpFile& ar);
    
    //! create a shallow copy
    void ShallowCopy(DumpStream& dmp, bool bsave);
    
    //! update state
    void Update();
    
    //! Reset data
    void Reset();
    
public:
    int	m_nRBa;		//!< rigid body A that the joint connects
    int	m_nRBb;		//!< rigid body B that the joint connects
    
    vec3d	m_q0;	//! initial position of joint
    vec3d	m_qa0;	//! initial relative position vector of joint w.r.t. A
    vec3d	m_qb0;	//! initial relative position vector of joint w.r.t. B
    
    vec3d	m_e0[3];	//! initial joint basis
    vec3d	m_ea0[3];	//! initial joint basis w.r.t. A
    vec3d	m_eb0[3];	//! initial joint basis w.r.t. B
    
    vec3d	m_F;	//! constraining force
    vec3d	m_L;	//! Lagrange multiplier for constraining force
    double	m_eps;	//! penalty factor for constraining force
    
    vec3d	m_M;	//! constraining moment
    vec3d	m_U;	//! Lagrange multiplier for constraining moment
    double	m_ups;	//! penalty factor for constraining moment
    
    double	m_atol;	//! augmented Lagrangian tolerance
    double  m_gtol; //! augmented Lagrangian gap tolerance
    double  m_qtol; //! augmented Lagrangian angular gap tolerance
    int     m_naugmin;  //! minimum number of augmentations
    int     m_naugmax;  //! maximum number of augmentations
    
    double  m_dp;   //! prescribed translation
    double  m_qp;   //! prescribed rotation
    FELoadCurve* m_pdLC;    // pointer to loadcurve for prescribed translation
    FELoadCurve* m_pqLC;    // pointer to loadcurve for prescribed rotation
    double  m_dpscl;    //! scale factor for prescribed translation
    double  m_qpscl;    //! scale factor for prescribed rotation
    double  m_time;
    bool    m_bd;   //! flag for prescribing translation
    bool    m_bq;   //! flag for prescribing rotation
    double  m_Fp;   //! prescribed force
    double  m_Mp;   //! prescribed moment
    
protected:
    int		m_nID;	//!< ID of rigid joint
    bool	m_binit;
    double  m_alpha;    //! alpha from solver
    
    DECLARE_PARAMETER_LIST();
};

#endif /* defined(__FEBioMech__FERigidCylindricalJoint__) */
