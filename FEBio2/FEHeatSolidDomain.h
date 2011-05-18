#pragma once
#include "FECore/FESolidDomain.h"
#include "FEHeatSolver.h"

//-----------------------------------------------------------------------------
//! domain class for 3D heat elements
class FEHeatSolidDomain : public FESolidDomain
{
public:
	FEHeatSolidDomain(FEMesh* pm, FEMaterial* pmat) : FESolidDomain(FE_HEAT_SOLID_DOMAIN, pm, pmat) {}

	FEDomain* Clone()
	{
		FEHeatSolidDomain* pd = new FEHeatSolidDomain(m_pMesh, m_pMat);
		pd->m_Elem = m_Elem; pd->m_pMesh = m_pMesh; pd->m_Node = m_Node;
		return pd;
	}

	//! Unpack solid element data
	void UnpackElement(FEElement& el, unsigned int nflag = FE_UNPACK_ALL);
	void UnpackLM(FEElement& el);

	void HeatStiffnessMatrix(FEHeatSolver* psolver);

protected:
	//! calculate the conductive element stiffness matrix
	void ConductionStiffness(FEM& fem, FESolidElement& el, matrix& ke);

	//! calculate the capacitance element stiffness matrix
	void CapacitanceStiffness(FEM& fem, FESolidElement& el, matrix& ke);
};
