#include "stdafx.h"
#include "FEBioMechPlot.h"
#include "FEDamageNeoHookean.h"
#include "FEDamageTransIsoMooneyRivlin.h"
#include "FEFatigueMaterial.h"
#include "FERemodelingElasticMaterial.h"
#include "FERigidSolidDomain.h"
#include "FERigidShellDomain.h"
#include "FEElasticShellDomainOld.h"
#include "FEElasticEASShellDomain.h"
#include "FEElasticANSShellDomain.h"
#include "FEElasticMixture.h"
#include "FEElasticMultigeneration.h"
#include "FEUT4Domain.h"
#include "FEBioPlot/FEBioPlotFile.h"
#include "FEContactSurface.h"
#include "FERigidBody.h"
#include <FECore/FESPRProjection.h>
#include "FEUncoupledElasticMixture.h"
#include "FERigidMaterial.h"
#include "FEVolumeConstraint.h"
#include "FEMicroMaterial.h"
#include "FEMicroMaterial2O.h"
#include "FEFacet2FacetSliding.h"
#include "FEMortarSlidingContact.h"
#include "FERigidSystem.h"
#include "FEMechModel.h"

//=============================================================================
//                            N O D E   D A T A
//=============================================================================

//-----------------------------------------------------------------------------
bool FEPlotNodeVelocity::Save(FEMesh& m, FEDataStream& a)
{
	FEModel* fem = GetFEModel();
	const int dof_VX = fem->GetDOFIndex("vx");
	const int dof_VY = fem->GetDOFIndex("vy");
	const int dof_VZ = fem->GetDOFIndex("vz");

	writeNodalValues(m, a, [=](const FENode& node) {
		return node.get_vec3d(dof_VX, dof_VY, dof_VZ);
	});
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotNodeAcceleration::Save(FEMesh& m, FEDataStream& a)
{
	writeNodalValues(m, a, [](const FENode& node) {
		return node.m_at;
	});
	return true;
}

//-----------------------------------------------------------------------------
//! Store nodal reaction forces
bool FEPlotNodeReactionForces::Save(FEMesh& m, FEDataStream& a)
{
	writeNodalValues(m, a, [](const FENode& node) {
		return node.m_Fr;
	});
	return true;
}

//=============================================================================
//                       S U R F A C E    D A T A
//=============================================================================

//-----------------------------------------------------------------------------
// Plot contact gap
bool FEPlotContactGap::Save(FESurface& surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
 
	writeAverageElementValue(surf, a, [=](const FEMaterialPoint& mp) {
		const FEContactMaterialPoint* pt = mp.ExtractData<FEContactMaterialPoint>();
		return (pt ? pt->m_gap : 0.0);
	});
    return true;
}

//-----------------------------------------------------------------------------
// Plot vector gap
bool FEPlotVectorGap::Save(FESurface& surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
	writeElementValue(surf, a, [=](int nface) {
		vec3d gn;
		pcs->GetVectorGap(nface, gn);
		return gn;
	});
    return true;
}

//-----------------------------------------------------------------------------
// Plot contact pressure
bool FEPlotContactPressure::Save(FESurface &surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
	writeAverageElementValue(surf, a, [](const FEMaterialPoint& mp) {
		const FEContactMaterialPoint* pt = mp.ExtractData<FEContactMaterialPoint>();
		return (pt ? pt->m_Ln : 0.0);
	});

    return true;
}

//-----------------------------------------------------------------------------
// Plot contact traction
bool FEPlotContactTraction::Save(FESurface &surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
	writeElementValue(surf, a, [=](int nface) {
		vec3d tn;
		pcs->GetContactTraction(nface, tn);
		return tn;
	});    
    return true;
}

//-----------------------------------------------------------------------------
// Plot nodal contact gap
bool FEPlotNodalContactGap::Save(FESurface& surf, FEDataStream& a)
{
	FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
	if (pcs == 0) return false;

	writeNodalProjectedElementValues(surf, a, [](const FEMaterialPoint& mp) {
		const FEContactMaterialPoint* pt = mp.ExtractData<FEContactMaterialPoint>();
		return (pt ? pt->m_gap : 0.0);
	});
	return true;
}

//-----------------------------------------------------------------------------
// Plot nodal vector gap
bool FEPlotNodalVectorGap::Save(FESurface &surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
    int NF = pcs->Elements();
    vec3d gn[FEElement::MAX_NODES];
    for (int j=0; j<NF; ++j)
    {
        FESurfaceElement& el = pcs->Element(j);
        pcs->GetNodalVectorGap(j, gn);
        
        // store in archive
        int ne = el.Nodes();
        for (int k=0; k<ne; ++k)
        {
			a << gn[k];
        }
    }
    
    return true;
}

//-----------------------------------------------------------------------------
// Plot nodal contact pressure
bool FEPlotNodalContactPressure::Save(FESurface &surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
	writeNodalProjectedElementValues(surf, a, [](const FEMaterialPoint& mp) {
		const FEContactMaterialPoint* pt = mp.ExtractData<FEContactMaterialPoint>();
		return (pt ? pt->m_Ln : 0.0);
	});

	return true;
}

//-----------------------------------------------------------------------------
// Plot nodal contact traction
bool FEPlotNodalContactTraction::Save(FESurface &surf, FEDataStream& a)
{
	FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
	if (pcs == 0) return false;

	int NF = pcs->Elements();
	vec3d tn[FEElement::MAX_NODES];
	for (int j=0; j<NF; ++j)
	{
		FESurfaceElement& el = pcs->Element(j);
		pcs->GetNodalContactTraction(j, tn);

		// store in archive
		int ne = el.Nodes();
		for (int k=0; k<ne; ++k)
		{
			a << tn[k];
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Plot surface traction
bool FEPlotSurfaceTraction::Save(FESurface &surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
	writeElementValue(surf, a, [=](int nface) {
		vec3d tn;
		pcs->GetSurfaceTraction(nface, tn);
		return tn;
	});
    
    return true;
}

//-----------------------------------------------------------------------------
// Plot nodal contact traction
bool FEPlotNodalSurfaceTraction::Save(FESurface &surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
    int NF = pcs->Elements();
    vec3d tn[FEElement::MAX_NODES];
    for (int j=0; j<NF; ++j)
    {
        FESurfaceElement& el = pcs->Element(j);
        pcs->GetNodalSurfaceTraction(j, tn);
        
        // store in archive
        int ne = el.Nodes();
        for (int k=0; k<ne; ++k)
        {
			a << tn[k];
        }
    }
    
    return true;
}

//-----------------------------------------------------------------------------
// Plot stick status
bool FEPlotStickStatus::Save(FESurface& surf, FEDataStream& a)
{
    FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
    if (pcs == 0) return false;
    
	writeElementValue(surf, a, [=](int nface) {
		double gn;
		pcs->GetStickStatus(nface, gn);
        return gn;
	});

    return true;
}

//-----------------------------------------------------------------------------
bool FEPlotContactForce::Save(FESurface &surf, FEDataStream &a)
{
	FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
	if (pcs == 0) return false;
    
	vec3d fn = pcs->GetContactForce();
	a << fn;
    
	return true;
}

//-----------------------------------------------------------------------------
// Plot contact area
bool FEPlotContactArea::Save(FESurface &surf, FEDataStream& a)
{
	FEContactSurface* pcs = dynamic_cast<FEContactSurface*>(&surf);
	if (pcs == 0) return false;

	double area = pcs->GetContactArea();
	a << area;

	return true;
}

//-----------------------------------------------------------------------------
// Plot contact penalty parameter
bool FEPlotContactPenalty::Save(FESurface& surf, FEDataStream& a)
{
	FEFacetSlidingSurface* ps = dynamic_cast<FEFacetSlidingSurface*>(&surf);
	if (ps)
	{
		writeAverageElementValue(surf, a, [](const FEMaterialPoint& mp) {
			const FEFacetSlidingSurface::Data& pt = *mp.ExtractData<FEFacetSlidingSurface::Data>();
			return pt.m_eps;
		});
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
bool FEPlotMortarContactGap::Save(FESurface& S, FEDataStream& a)
{
	FEMortarSlidingSurface* ps = dynamic_cast<FEMortarSlidingSurface*>(&S);
	if (ps)
	{
		writeNodalValues(S, a, [=](int i) {
			vec3d vA = ps->m_nu[i];
			vec3d gA = ps->m_gap[i];
			return gA*vA;
		});
		return true;
	}
	else return false;
}

//-----------------------------------------------------------------------------
bool FEPlotEnclosedVolume::Save(FESurface &surf, FEDataStream &a)
{
    FESurface* pcs = &surf;
    if (pcs == 0) return false;
    
    // Evaluate this field only for a specific domain, by checking domain name
    if (pcs->GetName() != m_szdom) return false;

	writeSummedElementValue(surf, a, [=](const FEMaterialPoint& mp) {
		FESurfaceElement& el = static_cast<FESurfaceElement&>(*mp.m_elem);
		int n = mp.m_index;
		vec3d xi = pcs->Local2Global(el, n);
		vec3d g[2];
		double wi = el.GaussWeights()[n];
		pcs->CoBaseVectors(el, n, g);
		return xi*(g[0] ^ g[1])*(wi / 3);
	});
    return true;
}

//=============================================================================
//							D O M A I N   D A T A
//=============================================================================

//-----------------------------------------------------------------------------
bool FEPlotElementVelocity::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    if ((pme == 0) || pme->IsRigid()) return false;

	writeAverageElementValue(dom, a, [](const FEMaterialPoint& mp) {
		const FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
		return pt.m_v;
	});

    return true;
}

//-----------------------------------------------------------------------------
bool FEPlotElementAcceleration::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;

	writeAverageElementValue(dom, a, [](const FEMaterialPoint& mp) {
		const FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
		return pt.m_a;
	});

	return true;
}

//=============================================================================
class FEStress
{
public:
	mat3ds operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint* pt = mp.ExtractData<FEElasticMaterialPoint>();
		return (pt ? pt->m_s : mat3ds(0));
	}
};

//-----------------------------------------------------------------------------
//! Store the average stresses for each element. 
bool FEPlotElementStress::Save(FEDomain& dom, FEDataStream& a)
{
	FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
	if ((pme == 0) || pme->IsRigid()) return false;
	writeAverageElementValue(dom, a, FEStress());

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotSPRStresses::Save(FEDomain& dom, FEDataStream& a)
{
	// For now, this is only available for solid domains
	if (dom.Class() != FE_DOMAIN_SOLID) return false;

	// get the domain
	FESolidDomain& sd = static_cast<FESolidDomain&>(dom);
	writeSPRElementValueMat3ds(sd, a, FEStress());

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotSPRLinearStresses::Save(FEDomain& dom, FEDataStream& a)
{
	// For now, this is only available for solid domains
	if (dom.Class() != FE_DOMAIN_SOLID) return false;

	// get the domain
	FESolidDomain& sd = static_cast<FESolidDomain&>(dom);
	writeSPRElementValueMat3ds(sd, a, FEStress(), 1);

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotNodalStresses::Save(FEDomain& dom, FEDataStream& a)
{
	writeNodalProjectedElementValues(dom, a, FEStress());
	return true;
}

//=============================================================================
//! Store the uncoupled pressure for each element.
bool FEPlotElementUncoupledPressure::Save(FEDomain& dom, FEDataStream& a)
{
    FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
    if ((pme == 0) || pme->IsRigid()) return false;
    FEUncoupledMaterial* pmu = dynamic_cast<FEUncoupledMaterial*>(pme);
    if (pmu == 0) return false;
    
    // write element data
	writeAverageElementValue(dom, a, [=](const FEMaterialPoint& mp) {
		const FEElasticMaterialPoint* pt = mp.ExtractData<FEElasticMaterialPoint>();
		if (pt == 0) return 0.0;
		return -pmu->UJ(pt->m_J);   // use negative sign to get positive pressure in compression
	});
    
    return true;
}

//-----------------------------------------------------------------------------
//! Store the average deformation Hessian (G) for each element. 

class FEMicro2OG
{
public:
	tens3drs operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint2O& pt2O = *(mp.ExtractData<FEElasticMaterialPoint2O>());
		return pt2O.m_G;
	}
};

bool FEPlotElementGnorm::Save(FEDomain& dom, FEDataStream& a)
{
	FEElasticMaterial2O* pme = dynamic_cast<FEElasticMaterial2O*>(dom.GetMaterial()->GetElasticMaterial());
	if (pme == 0) return false;

	writeAverageElementValue(dom, a, FEMicro2OG(), [](const tens3drs& m) { return m.tripledot(m); });

	return true;
}

//-----------------------------------------------------------------------------
//! Store the norm of the average Cauchy stress for each element. 
bool FEPlotElementsnorm::Save(FEDomain& dom, FEDataStream& a)
{
	FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
	if ((pme == 0) || pme->IsRigid()) return false;

	writeAverageElementValue(dom, a, FEStress(), [](const mat3ds& s) { return sqrt(s.dotdot(s)); });
	
	return true;
}

//-----------------------------------------------------------------------------
//! Store the norm of the average PK1 stress for each element.

class FEMicro1OPK1Stress
{
public:
	FEMicro1OPK1Stress(FEMicroMaterial* pm) : m_mat(pm) {}
	mat3d operator()(const FEMaterialPoint& mp)
	{
		FEMaterialPoint& mp_noconst = const_cast<FEMaterialPoint&>(mp);
		FEMicroMaterialPoint* mmppt = mp_noconst.ExtractData<FEMicroMaterialPoint>();
		return m_mat->AveragedStressPK1(mmppt->m_rve, mp_noconst);
	}

private:
	FEMicroMaterial*	m_mat;
};

class FEMicro2OPK1Stress
{
public:
	mat3d operator()(const FEMaterialPoint& mp)
	{
		FEMaterialPoint& mp_noconst = const_cast<FEMaterialPoint&>(mp);
		FEMicroMaterialPoint2O* mmppt = mp_noconst.ExtractData<FEMicroMaterialPoint2O>();
		return mmppt->m_rve.AveragedStressPK1(mp_noconst);
	}
};

bool FEPlotElementPK1norm::Save(FEDomain& dom, FEDataStream& a)
{
	FEMicroMaterial* pm1O = dynamic_cast<FEMicroMaterial*>(dom.GetMaterial()->GetElasticMaterial());
	if (pm1O)
	{
		writeAverageElementValue(dom, a, FEMicro1OPK1Stress(pm1O), [](const mat3d& m) {return m.dotdot(m); });
		return true;
	}

	FEMicroMaterial2O* pm2O = dynamic_cast<FEMicroMaterial2O*>(dom.GetMaterial()->GetElasticMaterial());
	if (pm2O == 0)
	{
		writeAverageElementValue(dom, a, FEMicro2OPK1Stress(), [](const mat3d& m) {return m.dotdot(m); });
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//! Store the norm of the average PK1 stress moment for each element. 

class FEMicro2OQK1
{
public:
	tens3drs operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint2O& pt2O = *(mp.ExtractData<FEElasticMaterialPoint2O>());
		return pt2O.m_Q;
	}
};

bool FEPlotElementQK1norm::Save(FEDomain& dom, FEDataStream& a)
{
	FEElasticMaterial2O* pme = dynamic_cast<FEElasticMaterial2O*>(dom.GetMaterial()->GetElasticMaterial());
	if (pme == 0) return false;
	
	// write solid element data
	writeAverageElementValue(dom, a, FEMicro2OQK1(), [](const tens3drs& m) { return m.tripledot(m); });
	
	return true;
}

//-----------------------------------------------------------------------------
//! Element macro energy
bool FEPlotElementMicroEnergy::Save(FEDomain& dom, FEDataStream& a)
{
	FEMicroMaterial* pm1O = dynamic_cast<FEMicroMaterial*>(dom.GetMaterial()->GetElasticMaterial());
	if (pm1O)
	{
		writeAverageElementValue(dom, a, [](const FEMaterialPoint& mp) {
			const FEMicroMaterialPoint& mmpt = *(mp.ExtractData<FEMicroMaterialPoint>());
			return mmpt.m_micro_energy;
		});
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
//! Store the average elasticity for each element.

class FEElementElasticity
{
public:
	FEElementElasticity(FEElasticMaterial* pm) : m_mat(pm) {}
	tens4ds operator()(const FEMaterialPoint& mp)
	{
		return m_mat->Tangent(const_cast<FEMaterialPoint&>(mp));
	}
private:
	FEElasticMaterial*	m_mat;
};

bool FEPlotElementElasticity::Save(FEDomain& dom, FEDataStream& a)
{
    FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
    if ((pme == 0) || pme->IsRigid()) return false;

	writeAverageElementValue(dom, a, FEElementElasticity(pme));
	return true;
}

//-----------------------------------------------------------------------------
class FEStrainEnergy
{
public:
	FEStrainEnergy(FEElasticMaterial* pm) : m_mat(pm) {}
	double operator()(const FEMaterialPoint& mp)
	{
		return m_mat->StrainEnergyDensity(const_cast<FEMaterialPoint&>(mp));
	}
private:
	FEElasticMaterial*	m_mat;
};

bool FEPlotStrainEnergyDensity::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();

    if ((pme == 0) || pme->IsRigid()) return false;
    
	if (dom.Class() == FE_DOMAIN_SOLID)
	{
		FEStrainEnergy W(pme);
		writeAverageElementValue(dom, a, W);
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
class FEDevStrainEnergy
{
public:
	FEDevStrainEnergy(FEUncoupledMaterial* pm) : m_mat(pm) {}
	double operator()(const FEMaterialPoint& mp)
	{
		return m_mat->DevStrainEnergyDensity(const_cast<FEMaterialPoint&>(mp));
	}
private:
	FEUncoupledMaterial*	m_mat;
};

bool FEPlotDevStrainEnergyDensity::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    FEUncoupledMaterial* pmu = dynamic_cast<FEUncoupledMaterial*>(pme);
    if ((pme == 0) || pme->IsRigid() || (pmu == 0)) return false;
    
	if (dom.Class() == FE_DOMAIN_SOLID)
	{
		FEDevStrainEnergy devW(pmu);
		writeAverageElementValue(dom, a, devW);
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
class FESpecificStrainEnergy
{
public:
	double operator()(const FEMaterialPoint& mp)
	{
		const FERemodelingMaterialPoint* rpt = mp.ExtractData<FERemodelingMaterialPoint>();
		return (rpt ? rpt->m_sed / rpt->m_rhor : 0.0);
	}
};

bool FEPlotSpecificStrainEnergy::Save(FEDomain &dom, FEDataStream& a)
{
	if (dom.Class() != FE_DOMAIN_SOLID) return false;
	FESpecificStrainEnergy E;
	writeAverageElementValue(dom, a, E);

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotKineticEnergyDensity::Save(FEDomain &dom, FEDataStream& a)
{
    const int dof_VX = GetFEModel()->GetDOFIndex("vx");
    const int dof_VY = GetFEModel()->GetDOFIndex("vy");
    const int dof_VZ = GetFEModel()->GetDOFIndex("vz");
    const int dof_VU = GetFEModel()->GetDOFIndex("vu");
    const int dof_VV = GetFEModel()->GetDOFIndex("vv");
    const int dof_VW = GetFEModel()->GetDOFIndex("vw");
    
    FEMesh& mesh = *dom.GetMesh();
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
        for (int i=0; i<bd.Elements(); ++i)
        {
            FESolidElement& el = bd.Element(i);
            double *H;
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d vt[FEElement::MAX_NODES];
            vec3d vn[FEElement::MAX_NODES];
            for (int j=0; j<el.Nodes(); ++j) {
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
            {
                H = el.H(j);
                vn[j] = vec3d(0, 0, 0);
                for (int k=0; k<el.Nodes(); ++k)
                    vn[j] += vt[k]*H[k];
            }
            
            // integrate kinetic energy
            double ew = 0;
            double V = 0;
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);
                double detJ = bd.detJ0(el, j)*gw[j];
                V += detJ;
                ew += vn[j]*vn[j]*(dens(mp)/2*detJ);
            }
            
            a << ew/V;
        }
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
        for (int i=0; i<bd->Elements(); ++i)
        {
            FEShellElement& el = bd->Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d vt[FEElement::MAX_NODES];
            vec3d wt[FEElement::MAX_NODES];
            vec3d vn[FEElement::MAX_NODES];
            for (int j=0; j<el.Nodes(); ++j) {
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
                wt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VU, dof_VV, dof_VW);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
                vn[j] = bd->evaluate(el, vt, wt, j);
            
            // integrate kinetic energy
            double ew = 0;
            double V = 0;
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);

                double detJ = bd->detJ0(el, j)*gw[j];
                V += detJ;
                ew += vn[j]*vn[j]*(dens(mp)/2*detJ);
            }
            
            a << ew/V;
        }
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// TODO: Should I call the density for remodeling materials something else? 
//       Or maybe the FEElasticMaterialPoint should define a density parameter
//       that will be updated by the materials to define the current density?

class FEDensity
{
public:
	FEDensity(FEElasticMaterial* pm) : m_mat(pm) {}
	double operator()(const FEMaterialPoint& mp)
	{
		FEParamDouble& rho0 = m_mat->Density();
		const FEElasticMaterialPoint& ep = *mp.ExtractData<FEElasticMaterialPoint>();
		double J = ep.m_F.det();
		return rho0(mp) / J;
	}
private:
	FEElasticMaterial*	m_mat;
};

class FERemodelingDensity
{
public:
	double operator()(const FEMaterialPoint& mp)
	{
		const FERemodelingMaterialPoint* pt = (mp.ExtractData<FERemodelingMaterialPoint>());
		return (pt ? pt->m_rhor : 0.0);
	}
};

bool FEPlotDensity::Save(FEDomain &dom, FEDataStream& a)
{
	if (dom.Class() == FE_DOMAIN_SOLID)
	{
		FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		FEElasticMaterial* em = dynamic_cast<FEElasticMaterial*>(bd.GetMaterial());
		if (em == 0) return false;

		FERemodelingElasticMaterial* rm = dynamic_cast<FERemodelingElasticMaterial*>(em);
		if (rm)
		{
			FERemodelingDensity dens;
			writeAverageElementValue(dom, a, dens);
			return true;
		}
		else
		{
			FEDensity dens(em);
			writeAverageElementValue(dom, a, dens);
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
bool FEPlotElementStrainEnergy::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		writeIntegratedElementValue(bd, a, FEStrainEnergy(pme));
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
		writeIntegratedElementValue(*bd, a, FEStrainEnergy(pme));
		return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// integrated element kinetic energy
class FEKineticEnergyDensity
{
public:
	FEKineticEnergyDensity(FEElasticMaterial* pm) : m_mat(pm) {}
	double operator()(const FEMaterialPoint& mp)
	{
		FEParamDouble& dens = m_mat->Density();
		const FEElasticMaterialPoint& ep = *(mp.ExtractData<FEElasticMaterialPoint>());
		return 0.5*(ep.m_v*ep.m_v)*dens(mp);
	}
private:
	FEElasticMaterial*	m_mat;
};

bool FEPlotElementKineticEnergy::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		writeIntegratedElementValue(bd, a, FEKineticEnergyDensity(pme));
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
		writeIntegratedElementValue(*bd, a, FEKineticEnergyDensity(pme));
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool FEPlotElementCenterOfMass::Save(FEDomain &dom, FEDataStream& a)
{
	FEMaterial* pmm = dom.GetMaterial();
	FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
	if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();

	if ((pme == 0) || pme->IsRigid()) return false;

	FEParamDouble& dens = pme->Density();

	if (dom.Class() == FE_DOMAIN_SOLID)
	{
		FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		for (int i = 0; i<bd.Elements(); ++i)
		{
			FESolidElement& el = bd.Element(i);
			double* gw = el.GaussWeights();

			// integrate zeroth and first mass moments
			vec3d ew = vec3d(0, 0, 0);
			double m = 0;
			for (int j = 0; j<el.GaussPoints(); ++j)
			{
				FEElasticMaterialPoint& pt = *(el.GetMaterialPoint(j)->ExtractData<FEElasticMaterialPoint>());
				double detJ = bd.detJ0(el, j)*gw[j];
				ew += pt.m_rt*(dens(pt)*detJ);
				m += dens(pt)*detJ;
			}

			a << ew / m;
		}
		return true;
	}
	else if (dom.Class() == FE_DOMAIN_SHELL)
	{
		FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
		if (bd == 0) return false;
		for (int i = 0; i<bd->Elements(); ++i)
		{
			FEShellElement& el = bd->Element(i);
			double* gw = el.GaussWeights();

			// integrate zeroth and first mass moments
			vec3d ew = vec3d(0, 0, 0);
			double m = 0;
			for (int j = 0; j<el.GaussPoints(); ++j)
			{
				FEElasticMaterialPoint& pt = *(el.GetMaterialPoint(j)->ExtractData<FEElasticMaterialPoint>());
				double detJ = bd->detJ0(el, j)*gw[j];
				ew += pt.m_rt*(dens(pt)*detJ);
				m += dens(pt)*detJ;
			}

			a << ew / m;
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
class FEElementLinearMomentum
{
public:
	FEElementLinearMomentum(FEElasticMaterial* pm) : m_mat(pm) {}
	vec3d operator()(const FEMaterialPoint& mp)
	{
		FEParamDouble& dens = m_mat->Density();
		const FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
		return pt.m_v*dens(mp);
	}

private:
	FEElasticMaterial*	m_mat;
};

bool FEPlotElementLinearMomentum::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		writeIntegratedElementValue(bd, a, FEElementLinearMomentum(pme));
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
		writeIntegratedElementValue(*bd, a, FEElementLinearMomentum(pme));
		return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// Integrated element angular momentum
class FEElementAngularMomentum
{
public:
	FEElementAngularMomentum(FEElasticMaterial* pm) : m_mat(pm) {}
	vec3d operator()(const FEMaterialPoint& mp)
	{
		FEParamDouble& dens = m_mat->Density();
		const FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
		return (pt.m_rt ^ pt.m_v)*dens(mp);
	}

private:
	FEElasticMaterial*	m_mat;
};

bool FEPlotElementAngularMomentum::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		writeIntegratedElementValue(bd, a, FEElementAngularMomentum(pme));
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
		writeIntegratedElementValue(*bd, a, FEElementAngularMomentum(pme));
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
class FEElementStressPower
{
public:
	double operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint& ep = *mp.ExtractData<FEElasticMaterialPoint>();
		return  ep.m_s.dotdot(ep.m_L.sym())*ep.m_J;
	}
};

bool FEPlotElementStressPower::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		writeIntegratedElementValue(bd, a, FEElementStressPower());
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
		writeIntegratedElementValue(*bd, a, FEElementStressPower());
		return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
class FECurrentElementStrainEnergy
{
public:
	double operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint& ep = *mp.ExtractData<FEElasticMaterialPoint>();
		return ep.m_Wt;
	}
};

bool FEPlotCurrentElementStrainEnergy::Save(FEDomain &dom, FEDataStream& a)
{
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
		writeIntegratedElementValue(bd, a, FECurrentElementStrainEnergy());
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
		writeIntegratedElementValue(*bd, a, FECurrentElementStrainEnergy());
		return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool FEPlotCurrentElementKineticEnergy::Save(FEDomain &dom, FEDataStream& a)
{
    const int dof_VX = GetFEModel()->GetDOFIndex("vx");
    const int dof_VY = GetFEModel()->GetDOFIndex("vy");
    const int dof_VZ = GetFEModel()->GetDOFIndex("vz");
    const int dof_VU = GetFEModel()->GetDOFIndex("vu");
    const int dof_VV = GetFEModel()->GetDOFIndex("vv");
    const int dof_VW = GetFEModel()->GetDOFIndex("vw");
    
    FEMesh& mesh = *dom.GetMesh();
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    const int NELN = FEElement::MAX_NODES;
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
        for (int i=0; i<bd.Elements(); ++i)
        {
            FESolidElement& el = bd.Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d vt[NELN], vn[NELN];
            for (int j=0; j<el.Nodes(); ++j)
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
                vn[j] = el.Evaluate(vt, j);
            
            // integrate kinetic energy
            double ew = 0;
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);

                double detJ = bd.detJ0(el, j)*gw[j]*dens(mp)/2;
                ew += vn[j]*vn[j]*detJ;
            }
            
            a << ew;
        }
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
        for (int i=0; i<bd->Elements(); ++i)
        {
            FEShellElement& el = bd->Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d vt[NELN], wt[NELN], vn[NELN];
            for (int j=0; j<el.Nodes(); ++j) {
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
                wt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VU, dof_VV, dof_VW);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
                vn[j] = bd->evaluate(el, vt, wt, j);
            
            // integrate kinetic energy
            double ew = 0;
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);

                double detJ = bd->detJ0(el, j)*gw[j]*dens(mp)/2;
                ew += vn[j]*vn[j]*detJ;
            }
            
            a << ew;
        }
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool FEPlotCurrentElementCenterOfMass::Save(FEDomain &dom, FEDataStream& a)
{
    const int dof_SX = GetFEModel()->GetDOFIndex("sx");
    const int dof_SY = GetFEModel()->GetDOFIndex("sy");
    const int dof_SZ = GetFEModel()->GetDOFIndex("sz");
    
    FEMesh& mesh = *dom.GetMesh();
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    const int NELN = FEElement::MAX_NODES;
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
        for (int i=0; i<bd.Elements(); ++i)
        {
            FESolidElement& el = bd.Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal positions and velocities
            vec3d rt[NELN], rn[NELN];
            for (int j=0; j<el.Nodes(); ++j)
                rt[j] = mesh.Node(el.m_node[j]).m_rt;
            
            // evaluate positions at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
                rn[j] = el.Evaluate(rt, j);
            
            // integrate zeroth and first mass moment
            double ez = 0;
            vec3d ef = vec3d(0,0,0);
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);

                double detJ = bd.detJ0(el, j)*gw[j]*dens(mp);
                ez += detJ;
                ef += rn[j]*detJ;
            }
            
            a << ef/ez;
        }
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
        for (int i=0; i<bd->Elements(); ++i)
        {
            FEShellElement& el = bd->Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d rt[NELN], st[NELN], rn[NELN];
            for (int j=0; j<el.Nodes(); ++j) {
                rt[j] = mesh.Node(el.m_node[j]).m_rt;
                st[j] = mesh.Node(el.m_node[j]).m_r0 - mesh.Node(el.m_node[j]).m_d0 + mesh.Node(el.m_node[j]).get_vec3d(dof_SX, dof_SY, dof_SZ);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
                rn[j] = bd->evaluate(el, rt, st, j);
            
            // integrate zeroth and first mass moment
            double ez = 0;
            vec3d ef = vec3d(0,0,0);
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);

                double detJ = bd->detJ0(el, j)*gw[j]*dens(mp);
                ez += detJ;
                ef += rn[j]*detJ;
            }
            
            a << ef/ez;
        }
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool FEPlotCurrentElementLinearMomentum::Save(FEDomain &dom, FEDataStream& a)
{
    const int dof_VX = GetFEModel()->GetDOFIndex("vx");
    const int dof_VY = GetFEModel()->GetDOFIndex("vy");
    const int dof_VZ = GetFEModel()->GetDOFIndex("vz");
    const int dof_VU = GetFEModel()->GetDOFIndex("vu");
    const int dof_VV = GetFEModel()->GetDOFIndex("vv");
    const int dof_VW = GetFEModel()->GetDOFIndex("vw");
    
    FEMesh& mesh = *dom.GetMesh();
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    const int NELN = FEElement::MAX_NODES;

    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
        for (int i=0; i<bd.Elements(); ++i)
        {
            FESolidElement& el = bd.Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d vt[NELN], vn[NELN];
            for (int j=0; j<el.Nodes(); ++j) {
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
                vn[j] = el.Evaluate(vt, j);
            
            // integrate linear momentum
            vec3d ew = vec3d(0,0,0);
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);
                double detJ = bd.detJ0(el, j)*gw[j];
                ew += vn[j]*(dens(mp)*detJ);
            }
            
            a << ew;
        }
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
        for (int i=0; i<bd->Elements(); ++i)
        {
            FEShellElement& el = bd->Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d vt[NELN], wt[NELN], vn[NELN];
            for (int j=0; j<el.Nodes(); ++j) {
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
                wt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VU, dof_VV, dof_VW);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j)
                vn[j] = bd->evaluate(el, vt, wt, j);
            
            // integrate linear momentum
            vec3d ew = vec3d(0,0,0);
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);
                double detJ = bd->detJ0(el, j)*gw[j];
                ew += vn[j]*(dens(mp)*detJ);
            }
            
            a << ew;
        }
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool FEPlotCurrentElementAngularMomentum::Save(FEDomain &dom, FEDataStream& a)
{
    const int dof_SX = GetFEModel()->GetDOFIndex("sx");
    const int dof_SY = GetFEModel()->GetDOFIndex("sy");
    const int dof_SZ = GetFEModel()->GetDOFIndex("sz");
    const int dof_VX = GetFEModel()->GetDOFIndex("vx");
    const int dof_VY = GetFEModel()->GetDOFIndex("vy");
    const int dof_VZ = GetFEModel()->GetDOFIndex("vz");
    const int dof_SVX = GetFEModel()->GetDOFIndex("svx");
    const int dof_SVY = GetFEModel()->GetDOFIndex("svy");
    const int dof_SVZ = GetFEModel()->GetDOFIndex("svz");
    
    FEMesh& mesh = *dom.GetMesh();
    FEMaterial* pmm = dom.GetMaterial();
    FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmm);
    if (pme == nullptr) pme = dom.GetMaterial()->GetElasticMaterial();
    
    if ((pme == 0) || pme->IsRigid()) return false;
    
    FEParamDouble& dens = pme->Density();
    const int NELN = FEElement::MAX_NODES;
    
    if (dom.Class() == FE_DOMAIN_SOLID)
    {
        FESolidDomain& bd = static_cast<FESolidDomain&>(dom);
        for (int i=0; i<bd.Elements(); ++i)
        {
            FESolidElement& el = bd.Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal positions and velocities
            vec3d rt[NELN], rn[NELN];
            vec3d vt[NELN], vn[NELN];
            for (int j=0; j<el.Nodes(); ++j) {
                rt[j] = mesh.Node(el.m_node[j]).m_rt;
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j) {
                rn[j] = el.Evaluate(rt, j);
                vn[j] = el.Evaluate(vt, j);
            }
            
            // integrate angular momentum
            vec3d ew = vec3d(0,0,0);
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);
                double detJ = bd.detJ0(el, j)*gw[j];
                ew += (rn[j] ^ vn[j])*(dens(mp)*detJ);
            }
            
            a << ew;
        }
        return true;
    }
    else if (dom.Class() == FE_DOMAIN_SHELL)
    {
        FESSIShellDomain* bd = dynamic_cast<FESSIShellDomain*>(&dom);
        if (bd == 0) return false;
        for (int i=0; i<bd->Elements(); ++i)
        {
            FEShellElement& el = bd->Element(i);
            double* gw = el.GaussWeights();
            
            // get nodal velocities
            vec3d rt[NELN], st[NELN], rn[NELN];
            vec3d vt[NELN], wt[NELN], vn[NELN];
            for (int j=0; j<el.Nodes(); ++j) {
                rt[j] = mesh.Node(el.m_node[j]).m_rt;
                st[j] = mesh.Node(el.m_node[j]).m_r0 - mesh.Node(el.m_node[j]).m_d0 + mesh.Node(el.m_node[j]).get_vec3d(dof_SX, dof_SY, dof_SZ);
                vt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_VX, dof_VY, dof_VZ);
                wt[j] = mesh.Node(el.m_node[j]).get_vec3d(dof_SVX, dof_SVY, dof_SVZ);
            }
            
            // evaluate velocities at integration points
            for (int j=0; j<el.GaussPoints(); ++j) {
                rn[j] = bd->evaluate(el, rt, st, j);
                vn[j] = bd->evaluate(el, vt, wt, j);
            }
            
            // integrate angular momentum
            vec3d ew = vec3d(0,0,0);
            for (int j=0; j<el.GaussPoints(); ++j)
            {
				FEMaterialPoint& mp = *el.GetMaterialPoint(j);
                double detJ = bd->detJ0(el, j)*gw[j];
                ew += (rn[j] ^ vn[j])*(dens(mp)*detJ);
            }
            
            a << ew;
        }
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool FEPlotRelativeVolume::Save(FEDomain &dom, FEDataStream& a)
{
	if (dom.Class() != FE_DOMAIN_SOLID) return false;

	writeAverageElementValue(dom, a, [](const FEMaterialPoint& mp) {
		const FEElasticMaterialPoint* pt = mp.ExtractData<FEElasticMaterialPoint>();
		return (pt ? pt->m_J : 0.0);
	});

	return true;
}

//-----------------------------------------------------------------------------
class FEFiberVector
{
public:
	vec3d operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
		vec3d ri;
		ri.x = pt.m_Q[0][0];
		ri.y = pt.m_Q[1][0];
		ri.z = pt.m_Q[2][0];
		vec3d r = pt.m_F*ri;
		return r;
	}
};

bool FEPlotFiberStretch::Save(FEDomain &dom, FEDataStream& a)
{
	FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
	if (pme == 0) return false;

	if (dom.Class() != FE_DOMAIN_SOLID) return false;
	writeAverageElementValue(dom, a, FEFiberVector(), [](const vec3d& r) -> double { return r.norm(); });

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotFiberVector::Save(FEDomain &dom, FEDataStream& a)
{
	FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
	if (pme == 0) return false;
	writeAverageElementValue(dom, a, FEFiberVector(), [](const vec3d& r) -> vec3d { vec3d n(r); n.unit(); return n; });

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotMaterialAxes::Save(FEDomain &dom, FEDataStream& a)
{
	FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
	if (pme == 0) return false;

	int BE = dom.Elements();
	for (int i = 0; i<BE; ++i)
	{
		FEElement& el = dom.ElementRef(i);

		// I cannot average the material axes since the average may not be orthogonal
		// Until I find a better option, I'll just export the first integration point.
		FEElasticMaterialPoint& pt = *el.GetMaterialPoint(0)->ExtractData<FEElasticMaterialPoint>();
		mat3d Qi = pt.m_Q;
		a << Qi;
	}
	return true;
}

//-----------------------------------------------------------------------------
// TODO: The factor Jm13 is not used. This doesn't look correct
class FEDevFiberStretch
{
public:
	double operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

		// get the deformation gradient
		const mat3d& F = pt.m_F;
		double J = pt.m_J;
		double Jm13 = pow(J, -1.0 / 3.0);

		// get the material fiber axis
		vec3d ri;
		ri.x = pt.m_Q[0][0];
		ri.y = pt.m_Q[1][0];
		ri.z = pt.m_Q[2][0];

		// apply deformation
		vec3d r = pt.m_F*ri;

		// calculate the deviatoric fiber stretch
		double lam = r.norm();
		return lam;
	}
};

bool FEPlotDevFiberStretch::Save(FEDomain &dom, FEDataStream& a)
{
	FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
	if (pme == 0) return false;

	if (dom.Class() != FE_DOMAIN_SOLID) return false;
	FEDevFiberStretch lam;
	writeAverageElementValue(dom, a, lam);
	return true;
}


//=============================================================================
// Principal components of stress

class FEPrincStresses
{
public:
	mat3dd operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint& ep = *mp.ExtractData<FEElasticMaterialPoint>();
		const mat3ds& s = ep.m_s;
		double l[3];
		s.exact_eigen(l);
		return mat3dd(l[0], l[1], l[2]);
	}
};

bool FEPlotSPRPrincStresses::Save(FEDomain& dom, FEDataStream& a)
{
	// For now, this is only available for solid domains
	if (dom.Class() != FE_DOMAIN_SOLID) return false;

	// get the domain
	FESolidDomain& sd = static_cast<FESolidDomain&>(dom);
	writeSPRElementValueMat3dd(sd, a, FEPrincStresses());

	return true;
}

//=============================================================================
//! Store the average Euler-lagrange strain
class FELagrangeStrain
{
public:
	mat3ds operator()(const FEMaterialPoint& mp)
	{
		const FEElasticMaterialPoint* pt = mp.ExtractData<FEElasticMaterialPoint>();
		if (pt == 0) return mat3ds(0, 0, 0, 0, 0, 0);

		mat3d C = pt->RightCauchyGreen();
		mat3ds E = ((C - mat3dd(1.0))*0.5).sym();
		return E;
	}
};

//-----------------------------------------------------------------------------
bool FEPlotLagrangeStrain::Save(FEDomain& dom, FEDataStream& a)
{
	FEElasticMaterial* pme = dom.GetMaterial()->GetElasticMaterial();
	if ((pme == 0) || pme->IsRigid()) return false;
	writeAverageElementValue(dom, a, FELagrangeStrain());
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotSPRLagrangeStrain::Save(FEDomain& dom, FEDataStream& a)
{
	// For now, this is only available for solid domains
	if (dom.Class() != FE_DOMAIN_SOLID) return false;
	FESolidDomain& sd = static_cast<FESolidDomain&>(dom);
	writeSPRElementValueMat3ds(sd, a, FELagrangeStrain());
	return true;
}

//-----------------------------------------------------------------------------
//! Store shell thicknesses
bool FEPlotShellThickness::Save(FEDomain &dom, FEDataStream &a)
{
	if (dom.Class() == FE_DOMAIN_SHELL)
	{
		FEShellDomain& sd = static_cast<FEShellDomain&>(dom);
		int NS = sd.Elements();
		for (int i=0; i<NS; ++i)
		{	
			FEShellElement& e = sd.Element(i);
			int n = e.Nodes();
			for (int j=0; j<n; ++j) a << e.m_ht[j];
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
//! Store shell directors
bool FEPlotShellDirector::Save(FEDomain &dom, FEDataStream &a)
{
    const int dof_X = GetFEModel()->GetDOFIndex("x");
    const int dof_Y = GetFEModel()->GetDOFIndex("y");
    const int dof_Z = GetFEModel()->GetDOFIndex("z");
	const int dof_U = GetFEModel()->GetDOFIndex("u");
	const int dof_V = GetFEModel()->GetDOFIndex("v");
	const int dof_W = GetFEModel()->GetDOFIndex("w");
    const int dof_SX = GetFEModel()->GetDOFIndex("sx");
    const int dof_SY = GetFEModel()->GetDOFIndex("sy");
    const int dof_SZ = GetFEModel()->GetDOFIndex("sz");
	if (dom.Class() == FE_DOMAIN_SHELL)
	{
		if (dynamic_cast<FEElasticShellDomainOld*>(&dom))
		{
			FEShellDomainOld& sd = static_cast<FEShellDomainOld&>(dom);
			int NS = sd.Elements();
			FEMesh& mesh = *sd.GetMesh();
			for (int i = 0; i<NS; ++i)
			{
				FEShellElementOld& e = sd.ShellElement(i);
				int n = e.Nodes();
				for (int j = 0; j<n; ++j)
				{
					FENode& nj = mesh.Node(e.m_node[j]);
					vec3d D = e.m_D0[j] + nj.get_vec3d(dof_U, dof_V, dof_W);
					a << D;
				}
			}
			return true;
		}
		else
		{
			FEShellDomain& sd = static_cast<FEShellDomain&>(dom);
			int NS = sd.Elements();
			FEMesh& mesh = *sd.GetMesh();
			for (int i=0; i<NS; ++i)
			{
				FEShellElement& e = sd.Element(i);
				int n = e.Nodes();
				for (int j=0; j<n; ++j)
				{
					FENode& nj = mesh.Node(e.m_node[j]);
					vec3d D = nj.m_d0 + nj.get_vec3d(dof_X, dof_Y, dof_Z) - nj.get_vec3d(dof_SX, dof_SY, dof_SZ);
					a << D;
				}
			}
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
bool FEPlotDamage::Save(FEDomain &dom, FEDataStream& a)
{
	int N = dom.Elements();
	FEElasticMaterial* pmat = dom.GetMaterial()->GetElasticMaterial();
	if (dynamic_cast<FEElasticMixture*>(pmat)||dynamic_cast<FEUncoupledElasticMixture*>(pmat))
	{
		int NC = pmat->Properties();
		for (int i=0; i<N; ++i)
		{
			FEElement& el = dom.ElementRef(i);

			float D = 0.f;
			int nint = el.GaussPoints();
			for (int j=0; j<nint; ++j)
			{
				FEElasticMixtureMaterialPoint& pt = *el.GetMaterialPoint(j)->ExtractData<FEElasticMixtureMaterialPoint>();
				for (int k=0; k<NC; ++k)
				{
					FEDamageMaterialPoint* ppd = pt.GetPointData(k)->ExtractData<FEDamageMaterialPoint>();
                    FEFatigueMaterialPoint* ppf = pt.GetPointData(k)->ExtractData<FEFatigueMaterialPoint>();
					if (ppd) D += (float) ppd->m_D;
                    else if (ppf) D += (float) ppf->m_D;
				}
			}
			D /= (float) nint;
			a.push_back(D);
		}
	}
    else if (dynamic_cast<FEElasticMultigeneration*>(pmat))
    {
        FEElasticMultigeneration* pmg = dynamic_cast<FEElasticMultigeneration*>(pmat);
        int NC = pmg->Properties();
        for (int i=0; i<N; ++i)
        {
            FEElement& el = dom.ElementRef(i);
            
            float D = 0.f;
            int nint = el.GaussPoints();
            for (int j=0; j<nint; ++j)
            {
                FEMultigenerationMaterialPoint& pt = *el.GetMaterialPoint(j)->ExtractData<FEMultigenerationMaterialPoint>();
                for (int k=0; k<NC; ++k)
                {
                    FEDamageMaterialPoint* ppd = pt.GetPointData(k)->ExtractData<FEDamageMaterialPoint>();
                    FEFatigueMaterialPoint* ppf = pt.GetPointData(k)->ExtractData<FEFatigueMaterialPoint>();
                    FEElasticMixtureMaterialPoint* pem = pt.GetPointData(k)->ExtractData<FEElasticMixtureMaterialPoint>();
                    if (ppd) D += (float) ppd->m_D;
                    else if (ppf) D += (float) ppf->m_D;
                    else if (pem)
                    {
                        int NE = (int)pem->m_w.size();
                        for (int l=0; l<NE; ++l)
                        {
                            FEDamageMaterialPoint* ppd = pem->GetPointData(l)->ExtractData<FEDamageMaterialPoint>();
                            FEFatigueMaterialPoint* ppf = pem->GetPointData(l)->ExtractData<FEFatigueMaterialPoint>();
                            if (ppd) D += (float) ppd->m_D;
                            else if (ppf) D += (float) ppf->m_D;
                        }
                    }
                }
            }
            D /= (float) nint;
            a.push_back(D);
        }
    }
	else
	{
		for (int i=0; i<N; ++i)
		{
			FEElement& el = dom.ElementRef(i);

			float D = 0.f;
			int nint = el.GaussPoints();
			for (int j=0; j<nint; ++j)
			{
				FEMaterialPoint& pt = *el.GetMaterialPoint(j);
				FEDamageMaterialPoint* ppd = pt.ExtractData<FEDamageMaterialPoint>();
                FEFatigueMaterialPoint* ppf = pt.ExtractData<FEFatigueMaterialPoint>();
				if (ppd) D += (float) ppd->m_D;
                else if (ppf) D += (float) ppf->m_D;
			}
			D /= (float) nint;
			a.push_back(D);
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
FEPlotNestedDamage::FEPlotNestedDamage(FEModel* pfem) : FEPlotDomainData(PLT_FLOAT, FMT_ITEM)
{
    m_pfem = pfem;
    m_nmat = -1;
}

//-----------------------------------------------------------------------------
// Resolve nested damage material by number
bool FEPlotNestedDamage::SetFilter(int nmat)
{
    m_nmat = nmat-1;
    return (m_nmat != -1);
}

//-----------------------------------------------------------------------------
bool FEPlotNestedDamage::Save(FEDomain &dom, FEDataStream& a)
{
    int N = dom.Elements();
    FEElasticMaterial* pmat = dom.GetMaterial()->GetElasticMaterial();
    if (dynamic_cast<FEElasticMixture*>(pmat)||dynamic_cast<FEUncoupledElasticMixture*>(pmat))
    {
        int NC = pmat->Properties();
        if ((m_nmat > -1) && (m_nmat < NC))
        {
            for (int i=0; i<N; ++i)
            {
                FEElement& el = dom.ElementRef(i);
                
                float D = 0.f;
                int nint = el.GaussPoints();
                for (int j=0; j<nint; ++j)
                {
                    FEElasticMixtureMaterialPoint& pt = *el.GetMaterialPoint(j)->ExtractData<FEElasticMixtureMaterialPoint>();
                    FEDamageMaterialPoint* ppd = pt.GetPointData(m_nmat)->ExtractData<FEDamageMaterialPoint>();
                    FEFatigueMaterialPoint* ppf = pt.GetPointData(m_nmat)->ExtractData<FEFatigueMaterialPoint>();
                    if (ppd) D += (float) ppd->m_D;
                    else if (ppf) D += (float) ppf->m_D;
                }
                D /= (float) nint;
                a.push_back(D);
            }
        }
    }
    else if (dynamic_cast<FEElasticMultigeneration*>(pmat))
    {
        FEElasticMultigeneration* pmg = dynamic_cast<FEElasticMultigeneration*>(pmat);
        int NC = pmg->Properties();
        if ((m_nmat > -1) && (m_nmat < NC))
        {
            for (int i=0; i<N; ++i)
            {
                FEElement& el = dom.ElementRef(i);
                
                float D = 0.f;
                int nint = el.GaussPoints();
                for (int j=0; j<nint; ++j)
                {
                    FEMultigenerationMaterialPoint& pt = *el.GetMaterialPoint(j)->ExtractData<FEMultigenerationMaterialPoint>();
                    FEDamageMaterialPoint* ppd = pt.GetPointData(m_nmat)->ExtractData<FEDamageMaterialPoint>();
                    FEFatigueMaterialPoint* ppf = pt.GetPointData(m_nmat)->ExtractData<FEFatigueMaterialPoint>();
                    FEElasticMixtureMaterialPoint* pem = pt.GetPointData(m_nmat)->ExtractData<FEElasticMixtureMaterialPoint>();
                    if (ppd) D += (float) ppd->m_D;
                    else if (ppf) D += (float) ppf->m_D;
                    else if (pem)
                    {
                        int NE = (int)pem->m_w.size();
                        for (int l=0; l<NE; ++l)
                        {
                            FEDamageMaterialPoint* ppd = pem->GetPointData(l)->ExtractData<FEDamageMaterialPoint>();
                            FEFatigueMaterialPoint* ppf = pem->GetPointData(l)->ExtractData<FEFatigueMaterialPoint>();
                            if (ppd) D += (float) ppd->m_D;
                            else if (ppf) D += (float) ppf->m_D;
                        }
                    }
                }
                D /= (float) nint;
                a.push_back(D);
            }
        }
    }
    else
    {
        for (int i=0; i<N; ++i)
        {
            FEElement& el = dom.ElementRef(i);
            
            float D = 0.f;
            int nint = el.GaussPoints();
            for (int j=0; j<nint; ++j)
            {
                FEMaterialPoint& pt = *el.GetMaterialPoint(j);
                FEDamageMaterialPoint* ppd = pt.ExtractData<FEDamageMaterialPoint>();
                FEFatigueMaterialPoint* ppf = pt.ExtractData<FEFatigueMaterialPoint>();
                if (ppd) D += (float) ppd->m_D;
                else if (ppf) D += (float) ppf->m_D;
            }
            D /= (float) nint;
            a.push_back(D);
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
bool FEPlotMixtureVolumeFraction::Save(FEDomain &dom, FEDataStream &a)
{
	// extract the mixture material
	FEMaterial* pmat = dom.GetMaterial();
	FEElasticMixture* pm = dynamic_cast<FEElasticMixture*>(pmat);
	if (pm == 0) return false;

	writeAverageElementValue(dom, a, [](const FEMaterialPoint& mp) {
		const FEElasticMixtureMaterialPoint& pt = *mp.ExtractData<FEElasticMixtureMaterialPoint>();
		return pt.m_w[0];
	});

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotUT4NodalStresses::Save(FEDomain& dom, FEDataStream& a)
{
	// make sure this is a UT4 domain
	FEUT4Domain* pd = dynamic_cast<FEUT4Domain*>(&dom);
	if (pd == 0) return false;

	// write the nodal values
	writeNodalValues(dom, a, [=](int i) {
		FEUT4Domain::UT4NODE& n = pd->UT4Node(i);
		return n.si;
	});

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotShellStrain::Save(FEDomain &dom, FEDataStream &a)
{
	FEShellDomain* sd = dynamic_cast<FEShellDomain*>(&dom);
	if (sd == 0) return false;

	FEShellDomainNew* newsd = dynamic_cast<FEShellDomainNew*>(sd);
	FEElasticEASShellDomain* easd = dynamic_cast<FEElasticEASShellDomain*>(newsd);
	FEElasticANSShellDomain* ansd = dynamic_cast<FEElasticANSShellDomain*>(newsd);
    int NE = sd->Elements();
    if (easd || ansd) 
	{
        for (int i=0; i<NE; ++i)
        {
            FEShellElementNew& el = newsd->ShellElement(i);
            int nint = el.GaussPoints();
            mat3ds E; E.zero();
            for (int j=0; j<nint; ++j) E += el.m_E[j];
            E /= nint;
                
            a << E;
        }
    }
    else 
	{
		for (int i = 0; i<NE; ++i)
        {
            FEShellElement& el = sd->Element(i);
            int nint = el.GaussPoints();
            mat3ds E; E.zero();
            for (int j=0; j<nint; ++j)
            {
                FEElasticMaterialPoint& pt = *(el.GetMaterialPoint(j)->ExtractData<FEElasticMaterialPoint>());
                E += pt.Strain();
            }
            E /= nint;
                
            a << E;
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
bool FEPlotShellRelativeVolume::Save(FEDomain &dom, FEDataStream &a)
{
	FEShellDomain* sd = dynamic_cast<FEShellDomain*>(&dom);
	if (sd == 0) return false;

    FEShellDomainNew* newsd = dynamic_cast<FEShellDomainNew*>(sd);
	FEElasticEASShellDomain* easd = dynamic_cast<FEElasticEASShellDomain*>(newsd);
	FEElasticANSShellDomain* ansd = dynamic_cast<FEElasticANSShellDomain*>(newsd);
    int NE = sd->Elements();
    if (easd || ansd) {
        for (int i=0; i<NE; ++i)
        {
            FEShellElementNew& el = newsd->ShellElement(i);
            int nint = el.GaussPoints();
            mat3ds E; E.zero();
            for (int j=0; j<nint; ++j) E += el.m_E[j];
            E /= nint;
            mat3ds C = mat3dd(1) + E*2;
            double J = sqrt(C.det());
                
            a << J;
        }
    }
    else {
        for (int i=0; i<NE; ++i)
        {
            FEShellElement& el = sd->Element(i);
            int nint = el.GaussPoints();
            mat3ds E; E.zero();
            for (int j=0; j<nint; ++j)
            {
                FEElasticMaterialPoint& pt = *(el.GetMaterialPoint(j)->ExtractData<FEElasticMaterialPoint>());
                E += pt.Strain();
            }
            E /= nint;
            mat3ds C = mat3dd(1) + E*2;
            double J = sqrt(C.det());
                
            a << J;
        }
    }
    return true;
}

//==============================================================================
//                  R I G I D   B O D Y   D A T A
//==============================================================================

//-----------------------------------------------------------------------------
bool FEPlotRigidDisplacement::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;
    
	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());

	// store the rigid body position
	// TODO: why do we not store displacement?
	a << rb.m_rt;
    
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidVelocity::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
    
	// store the rigid velocity
	a << rb.m_vt;
    
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidAcceleration::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
    
	// store rigid body acceleration
	a << rb.m_at;
    
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidRotation::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
	vec3d q = rb.GetRotation().GetRotationVector();
    
	// store rotation vector
	a << q;
    
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidAngularVelocity::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
    
	// store rigid angular velocity
	a << rb.m_wt;
    
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidAngularAcceleration::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
    
	// store angular acceleration
	a << rb.m_alt;
    
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidKineticEnergy::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
    vec3d v = rb.m_vt;
    double m = rb.m_mass;
    vec3d w = rb.m_wt;
	mat3d Rt = rb.GetRotation().RotationMatrix();
    mat3ds Jt = (Rt*rb.m_moi*Rt.transpose()).sym();
    double ke = ((v*v)*m + w*(Jt*w))/2;
    
	// store kinetic energy
	a << ke;
    
	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidLinearMomentum::Save(FEDomain& dom, FEDataStream& a)
{
    // get the rigid material
    FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

    // get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
    FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
    
    // store linear momentum (mass x velocity)
    a << rb.m_vt*rb.m_mass;
    
    return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidAngularMomentum::Save(FEDomain& dom, FEDataStream& a)
{
    // get the rigid material
    FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

    // get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
    FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());
    
    // store angular momentum (mass moment of inertia x angular velocity)
	mat3d Rt = rb.GetRotation().RotationMatrix();
    mat3ds Jt = (Rt*rb.m_moi*Rt.transpose()).sym();
    
    a << Jt*rb.m_wt;
    
    return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidEuler::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());

	// get the Euler angles
	double E[3];
	quat2euler(rb.GetRotation(), E);
    
	// store Euler
	a << E[0] << E[1] << E[2];
    
	return true;
}

//-----------------------------------------------------------------------------
// TODO: I think this already gets stored somewhere
bool FEPlotRigidRotationVector::Save(FEDomain& dom, FEDataStream& a)
{
	// get the rigid material
	FEMaterial* pm = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pm);
	if (prm == 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
	FERigidBody& rb = *rigid.Object(prm->GetRigidBodyID());

	// get the rotation vector and angle
	vec3d r = rb.GetRotation().GetRotationVector();
    
	// store rotation vector
	a << r;
    
	return true;
}

//=============================================================================
bool FEPlotRigidReactionForce::Save(FEDomain& dom, FEDataStream& a)
{
	// get the material
	FEMaterial* pmat = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pmat);
	if (prm == 0) return false;

	// get the rigid body ID
	int nrid = prm->GetRigidBodyID();
	if (nrid < 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
    FERigidBody& rb = *rigid.Object(nrid);

	a << rb.m_Fr;

	return true;
}

//-----------------------------------------------------------------------------
bool FEPlotRigidReactionTorque::Save(FEDomain& dom, FEDataStream& a)
{
	// get the material
	FEMaterial* pmat = dom.GetMaterial();
	FERigidMaterial* prm = dynamic_cast<FERigidMaterial*>(pmat);
	if (prm == 0) return false;

	// get the rigid body ID
	int nrid = prm->GetRigidBodyID();
	if (nrid < 0) return false;

	// get the rigid body
	FEMechModel& fem = static_cast<FEMechModel&>(*m_pfem);
	FERigidSystem& rigid = *fem.GetRigidSystem();
    FERigidBody& rb = *rigid.Object(nrid);

	a << rb.m_Mr;

	return true;
}
