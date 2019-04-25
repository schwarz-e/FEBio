/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2019 University of Utah, Columbia University, and others.

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

#pragma once
#include <FECore/FESurfaceLoad.h>
#include <FECore/FESurfaceMap.h>
#include "febiofluid_api.h"

//-----------------------------------------------------------------------------
//! FEFluidVelocity is a fluid surface that has a velocity
//! prescribed on it.  This routine simultaneously prescribes a
//! surface load and nodal prescribed velocities
class FEBIOFLUID_API FEFluidVelocity : public FESurfaceLoad
{
public:
    //! constructor
    FEFluidVelocity(FEModel* pfem);
    
    //! Set the surface to apply the load to
    void SetSurface(FESurface* ps) override;
    
    //! calculate traction stiffness (there is none)
    void StiffnessMatrix(FELinearSystem& LS, const FETimeInfo& tp) override {}
    
    //! calculate residual
    void Residual(FEGlobalVector& R, const FETimeInfo& tp) override;
    
    //! Unpack surface element data
    void UnpackLM(FEElement& el, vector<int>& lm);
    
    //! set the velocity
    void Update() override;
    
    //! initialization
    bool Init() override;
    
    //! activate
    void Activate() override;
    
private:
    double			m_scale;	//!< average velocity
    FESurfaceMap	m_VC;		//!< velocity boundary cards
    vector<vec3d>   m_VN;       //!< nodal velocities
    
public:
    bool            m_bpv;      //!< flag for prescribing nodal values

	FEDofList		m_dofW;
    int				m_dofEF;
    
    DECLARE_FECORE_CLASS();
};
