#include "stdafx.h"
#include "FEPeriodicBoundary2O.h"
#include "FEStiffnessMatrix.h"
#include "FECore/DumpFile.h"
#include "FECore/FEModel.h"
#include "FECore/FENormalProjection.h"
#include "FECore/log.h"

//-----------------------------------------------------------------------------
// Define sliding interface parameters
BEGIN_PARAMETER_LIST(FEPeriodicBoundary2O, FEContactInterface)
	ADD_PARAMETER(m_blaugon  , FE_PARAM_BOOL  , "laugon"   );
	ADD_PARAMETER(m_atol     , FE_PARAM_DOUBLE, "tolerance");
	ADD_PARAMETER(m_eps      , FE_PARAM_DOUBLE, "penalty"  );
	ADD_PARAMETER(m_btwo_pass, FE_PARAM_BOOL  , "two_pass" );
	ADD_PARAMETER(m_off      , FE_PARAM_VEC3D , "offset"   );
	ADD_PARAMETER(m_naugmin  , FE_PARAM_INT   , "minaug"   );
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
// FEPeriodicBoundary
//-----------------------------------------------------------------------------

FEPeriodicBoundary2O::FEPeriodicBoundary2O(FEModel* pfem) : FEContactInterface(pfem), m_ss(&pfem->GetMesh()), m_ms(&pfem->GetMesh())
{
	static int count = 1;
	SetID(count++);

	m_stol = 0.01;
	m_srad = 1.0;
	m_atol = 0;
	m_eps = 0;
	m_btwo_pass = false;
	m_off = vec3d(0,0,0);
	m_naugmin = 0;

	m_ss.SetSibling(&m_ms);
	m_ms.SetSibling(&m_ss);

	m_Fmacro.zero();
	m_Gmacro.zero();
}

//-----------------------------------------------------------------------------
bool FEPeriodicBoundary2O::Init()
{
	// create the surfaces
	if (m_ss.Init() == false) return false;
	if (m_ms.Init() == false) return false;

	return true;
}

//-----------------------------------------------------------------------------
void FEPeriodicBoundary2O::Activate()
{
	// don't forget to call the base class
	FEContactInterface::Activate();

	// project slave surface onto master surface
	ProjectSurface(m_ss, m_ms, false);
	ProjectSurface(m_ms, m_ss, false);
}

//-----------------------------------------------------------------------------
void FEPeriodicBoundary2O::CopyFrom(FESurfacePairInteraction* pci)
{
	// cast to a periodic boundary
	FEPeriodicBoundary2O& pb = dynamic_cast<FEPeriodicBoundary2O&>(*pci);

	// copy parameters
	GetParameterList() = pb.GetParameterList();

	// copy data
	m_ss.CopyFrom(pb.m_ss);
	m_ms.CopyFrom(pb.m_ms);
}

//-----------------------------------------------------------------------------
//! build the matrix profile for use in the stiffness matrix
// TODO: what if two_pass ??
void FEPeriodicBoundary2O::BuildMatrixProfile(FEStiffnessMatrix& K)
{
	FEModel& fem = *GetFEModel();
	FEMesh& mesh = fem.GetMesh();

	// get the DOFS
	const int dof_X = fem.GetDOFIndex("x");
	const int dof_Y = fem.GetDOFIndex("y");
	const int dof_Z = fem.GetDOFIndex("z");
	const int dof_RU = fem.GetDOFIndex("Ru");
	const int dof_RV = fem.GetDOFIndex("Rv");
	const int dof_RW = fem.GetDOFIndex("Rw");

	vector<int> lm(6*5);

	for (int j=0; j<m_ss.Nodes(); ++j)
	{
		FESurfaceElement& me = *m_ss.m_pme[j];
		int* en = &me.m_node[0];

		int n = me.Nodes();
		if (n == 3)
		{
			lm[6*(3+1)  ] = -1;
			lm[6*(3+1)+1] = -1;
			lm[6*(3+1)+2] = -1;
			lm[6*(3+1)+3] = -1;
			lm[6*(3+1)+4] = -1;
			lm[6*(3+1)+5] = -1;
		}

		lm[0] = m_ss.Node(j).m_ID[dof_X];
		lm[1] = m_ss.Node(j).m_ID[dof_Y];
		lm[2] = m_ss.Node(j).m_ID[dof_Z];
		lm[3] = m_ss.Node(j).m_ID[dof_RU];
		lm[4] = m_ss.Node(j).m_ID[dof_RV];
		lm[5] = m_ss.Node(j).m_ID[dof_RW];

		for (int k=0; k<n; ++k)
		{
			vector<int>& id = mesh.Node(en[k]).m_ID;
			lm[6*(k+1)  ] = id[dof_X];
			lm[6*(k+1)+1] = id[dof_Y];
			lm[6*(k+1)+2] = id[dof_Z];
			lm[6*(k+1)+3] = id[dof_RU];
			lm[6*(k+1)+4] = id[dof_RV];
			lm[6*(k+1)+5] = id[dof_RW];
		}

		K.build_add(lm);
	}
}

//-----------------------------------------------------------------------------
//! project surface
void FEPeriodicBoundary2O::ProjectSurface(FEPeriodicSurface& ss, FEPeriodicSurface& ms, bool bmove)
{
	int i;
	double rs[2];

	// get the slave's center of mass
	vec3d cs = ss.CenterOfMass();

	// get the master's center of mass
	vec3d cm = ms.CenterOfMass();

	// get the relative distance
	vec3d cr = cs - cm;

	// unit vector in direction of cr
	// this will serve as the projection distance
	vec3d cn(cr); cn.unit();

	// initialize projection data
	FENormalProjection np(ms);
	np.SetTolerance(m_stol);
	np.SetSearchRadius(m_srad);
	np.Init();

	// loop over all slave nodes
	for (i=0; i<ss.Nodes(); ++i)
	{
		FENode& node = ss.Node(i);

		// get the nodal position
		vec3d r0 = node.m_r0;

		// find the intersection with the master surface
		ss.m_pme[i] = np.Project3(r0, cn, rs);
		assert(ss.m_pme[i]);

		ss.m_rs[i][0] = rs[0];
		ss.m_rs[i][1] = rs[1];
	}
}

//-----------------------------------------------------------------------------
void FEPeriodicBoundary2O::Update(int niter)
{
	int i, j, ne;
	FESurfaceElement* pme;

	FEMesh& mesh = *m_ss.GetMesh();

	//vec3d us, um;
	vec3d ws, wm;
	//vec3d umi[FEElement::MAX_NODES];
	vec3d wmi[FEElement::MAX_NODES];

	// update gap functions
	int npass = (m_btwo_pass?2:1);
	for (int np=0; np<npass; ++np)
	{
		FEPeriodicSurface& ss = (np == 0? m_ss : m_ms);
		FEPeriodicSurface& ms = (np == 0? m_ms : m_ss);

		// off-set sign
		double s = (np==0?1.0:-1.0);

		int N = ss.Nodes();

		for (i=0; i<N; ++i)
		{
			// calculate the slave displacement
			FENode& node = ss.Node(i);
			//us = node.m_rt - node.m_r0;
			ws = node.m_rt - m_Fmacro*node.m_r0 - m_Gmacro.contractdyad1(node.m_r0)*0.5;

			// get the master element
			pme = ss.m_pme[i];

			// calculate the master displacement
			ne = pme->Nodes();
			for (j=0; j<ne; ++j)
			{
				FENode& node = ms.Node(pme->m_lnode[j]);
				wmi[j] = node.m_rt - m_Fmacro*node.m_r0 - m_Gmacro.contractdyad1(node.m_r0)*0.5;
			}
			
			wm = pme->eval(wmi, ss.m_rs[i][0], ss.m_rs[i][1]);

			// calculate gap function
			//ss.m_gap[i] = ws - wm + m_off*s;
			ss.m_gap[i] = ws - wm;
		}
	}
}

//-----------------------------------------------------------------------------
void FEPeriodicBoundary2O::ShallowCopy(DumpStream& dmp, bool bsave)
{
	m_ss.ShallowCopy(dmp, bsave);
	m_ms.ShallowCopy(dmp, bsave);
}

//-----------------------------------------------------------------------------
void FEPeriodicBoundary2O::ContactForces(FEGlobalVector& R)
{
	int j, k, l, m, n;
	int nseln, nmeln;

	double *Gr, *Gs;

	// jacobian
	double detJ;

	vec3d dxr, dxs;
	double* w;

	// natural coordinates of slave node in master element
	double r, s;

	// contact force
	vec3d tc;

	// shape function values
	double N[FEElement::MAX_NODES];

	// element contact force vector
	vector<double> fe;

	// the lm array for this force vector
	vector<int> lm;

	// the en array
	vector<int> en;

	vector<int> sLM;
	vector<int> mLM;

	vec3d r0[FEElement::MAX_NODES], rt[FEElement::MAX_NODES];

	int npass = (m_btwo_pass?2:1);
	for (int np=0; np<npass; ++np)
	{
		FEPeriodicSurface& ss = (np == 0? m_ss : m_ms);
		FEPeriodicSurface& ms = (np == 0? m_ms : m_ss);

		zero(ss.m_Fr);

		// loop over all slave facets
		int ne = ss.Elements();
		for (j=0; j<ne; ++j)
		{
			// get the slave element
			FESurfaceElement& sel = ss.Element(j);
			
			// get the elements LM vector
			ss.UnpackLM(sel, sLM);

			nseln = sel.Nodes();

			for (int i=0; i<nseln; ++i)
			{
				r0[i] = ss.GetMesh()->Node(sel.m_node[i]).m_r0;
				rt[i] = ss.GetMesh()->Node(sel.m_node[i]).m_rt;
			}
			w = sel.GaussWeights();

			// loop over slave element nodes (which are the integration points as well)
			for (n=0; n<nseln; ++n)
			{
				Gr = sel.Gr(n);
				Gs = sel.Gs(n);

				m = sel.m_lnode[n];

				// calculate jacobian
				dxr = dxs = vec3d(0,0,0);
				for (k=0; k<nseln; ++k)
				{
					dxr.x += Gr[k]*r0[k].x;
					dxr.y += Gr[k]*r0[k].y;
					dxr.z += Gr[k]*r0[k].z;

					dxs.x += Gs[k]*r0[k].x;
					dxs.y += Gs[k]*r0[k].y;
					dxs.z += Gs[k]*r0[k].z;
				}

				detJ = (dxr ^ dxs).norm();

				// get slave node contact force
				tc = ss.m_Lm[m] + ss.m_gap[m]*m_eps;
				ss.m_Tn[m] = tc;

				// get the master element
				FESurfaceElement& mel = *ss.m_pme[m];
				ms.UnpackLM(mel, mLM);

				nmeln = mel.Nodes();

				// isoparametric coordinates of the projected slave node
				// onto the master element
				r = ss.m_rs[m][0];
				s = ss.m_rs[m][1];

				// get the master shape function values at this slave node
				if (nmeln == 4)
				{
					// quadrilateral
					N[0] = 0.25*(1-r)*(1-s);
					N[1] = 0.25*(1+r)*(1-s);
					N[2] = 0.25*(1+r)*(1+s);
					N[3] = 0.25*(1-r)*(1+s);
				}
				else if (nmeln == 3)
				{
					// triangle
					N[0] = 1 - r - s;
					N[1] = r;
					N[2] = s;
				}
				else
				{
					assert(false);
				}

				// calculate force vector
				fe.resize(3*(nmeln+1));
				fe[0] = -detJ*w[n]*tc.x;
				fe[1] = -detJ*w[n]*tc.y;
				fe[2] = -detJ*w[n]*tc.z;
				for (l=0; l<nmeln; ++l)
				{
					fe[3*(l+1)  ] = detJ*w[n]*tc.x*N[l];
					fe[3*(l+1)+1] = detJ*w[n]*tc.y*N[l];
					fe[3*(l+1)+2] = detJ*w[n]*tc.z*N[l];
				}

				// fill the lm array
				lm.resize(3*(nmeln+1));
				lm[0] = sLM[n*3  ];
				lm[1] = sLM[n*3+1];
				lm[2] = sLM[n*3+2];

				for (l=0; l<nmeln; ++l)
				{
					lm[3*(l+1)  ] = mLM[l*3  ];
					lm[3*(l+1)+1] = mLM[l*3+1];
					lm[3*(l+1)+2] = mLM[l*3+2];
				}

				// fill the en array
				en.resize(nmeln+1);
				en[0] = sel.m_node[n];
				for (l=0; l<nmeln; ++l) en[l+1] = mel.m_node[l];

				// assemble into global force vector
				R.Assemble(en, lm, fe);

				// also store in the reaction force vector
				vec3d& fr = ss.m_Fr[m];
				fr.x += fe[0];
				fr.y += fe[1];
				fr.z += fe[2];
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FEPeriodicBoundary2O::ContactStiffness(FESolver* psolver)
{
	int j, k, l, n, m;
	int nseln, nmeln, ndof;

	matrix ke;

	const int MN = FEElement::MAX_NODES;
	vector<int> lm(3*(MN+1));
	vector<int> en(MN+1);

	double *Gr, *Gs, *w;
	vec3d rt[MN], r0[MN];

	vec3d rtm[MN];

	double detJ, r, s;
	vec3d dxr, dxs;
	double H[MN];

	vec3d gap, Lm, tc;

	// curvature tensor K
	double K[2][2] = {0};

//	double scale = -0.0035*m_fem.GetMesh().GetBoundingBox().radius();

	vector<int> sLM;
	vector<int> mLM;

	int npass = (m_btwo_pass?2:1);
	for (int np=0; np<npass; ++np)
	{
		FEPeriodicSurface& ss = (np == 0? m_ss : m_ms);
		FEPeriodicSurface& ms = (np == 0? m_ms : m_ss);

		// loop over all slave elements
		int ne = ss.Elements();
		for (j=0; j<ne; ++j)
		{
			FESurfaceElement& se = ss.Element(j);

			// get the element's LM vector
			ss.UnpackLM(se, sLM);

			nseln = se.Nodes();

			for (int i=0; i<nseln; ++i)
			{
				r0[i] = ss.GetMesh()->Node(se.m_node[i]).m_r0;
				rt[i] = ss.GetMesh()->Node(se.m_node[i]).m_rt;
			}

			w = se.GaussWeights();

			// loop over all integration points (that is nodes)
			for (n=0; n<nseln; ++n)
			{
				Gr = se.Gr(n);
				Gs = se.Gs(n);

				m = se.m_lnode[n];

				// calculate jacobian
				dxr = dxs = vec3d(0,0,0);
				for (k=0; k<nseln; ++k)
				{
					dxr.x += Gr[k]*r0[k].x;
					dxr.y += Gr[k]*r0[k].y;
					dxr.z += Gr[k]*r0[k].z;

					dxs.x += Gs[k]*r0[k].x;
					dxs.y += Gs[k]*r0[k].y;
					dxs.z += Gs[k]*r0[k].z;
				}

				detJ = (dxr ^ dxs).norm();

				// get the master element
				FESurfaceElement& me = *ss.m_pme[m];
				ms.UnpackLM(me, mLM);

				nmeln = me.Nodes();

				// get the master element node positions
				for (k=0; k<nmeln; ++k) rtm[k] = ms.GetMesh()->Node(me.m_node[k]).m_rt;

				// slave node natural coordinates in master element
				r = ss.m_rs[m][0];
				s = ss.m_rs[m][1];

				// get slave node normal force
				tc = ss.m_Lm[m] + ss.m_gap[m]*m_eps; //ss.T[m];

				// get the master shape function values at this slave node
				if (nmeln == 4)
				{
					// quadrilateral
					H[0] = 0.25*(1-r)*(1-s);
					H[1] = 0.25*(1+r)*(1-s);
					H[2] = 0.25*(1+r)*(1+s);
					H[3] = 0.25*(1-r)*(1+s);
				}
				else if (nmeln == 3)
				{
					// triangle
					H[0] = 1 - r - s;
					H[1] = r;
					H[2] = s;
				}
				else 
				{
					assert(false);
				}

				// number of degrees of freedom
				ndof = 3*(1 + nmeln);

				// fill stiffness matrix
				ke.resize(ndof, ndof); ke.zero();
				ke[0][0] = w[n]*detJ*m_eps;
				ke[1][1] = w[n]*detJ*m_eps;
				ke[2][2] = w[n]*detJ*m_eps;
				for (k=0; k<nmeln; ++k)
				{
					ke[0][3+3*k  ] = -w[n]*detJ*m_eps*H[k];
					ke[1][3+3*k+1] = -w[n]*detJ*m_eps*H[k];
					ke[2][3+3*k+2] = -w[n]*detJ*m_eps*H[k];

					ke[3+3*k  ][0] = -w[n]*detJ*m_eps*H[k];
					ke[3+3*k+1][1] = -w[n]*detJ*m_eps*H[k];
					ke[3+3*k+2][2] = -w[n]*detJ*m_eps*H[k];
				}
				for (k=0; k<nmeln; ++k)
					for (l=0; l<nmeln; ++l)
					{
						ke[3+3*k  ][3+3*l  ] = w[n]*detJ*m_eps*H[k]*H[l];
						ke[3+3*k+1][3+3*l+1] = w[n]*detJ*m_eps*H[k]*H[l];
						ke[3+3*k+2][3+3*l+2] = w[n]*detJ*m_eps*H[k]*H[l];
					}

				// create lm array
				lm[0] = sLM[n*3  ];
				lm[1] = sLM[n*3+1];
				lm[2] = sLM[n*3+2];

				for (k=0; k<nmeln; ++k)
				{
					lm[3*(k+1)  ] = mLM[k*3  ];
					lm[3*(k+1)+1] = mLM[k*3+1];
					lm[3*(k+1)+2] = mLM[k*3+2];
				}

				// create the en array
				en.resize(nmeln+1);
				en[0] = se.m_node[n];
				for (k=0; k<nmeln; ++k) en[k+1] = me.m_node[k];

				// assemble stiffness matrix
				psolver->AssembleStiffness(en, lm, ke);
			}
		}
	}
}

//-----------------------------------------------------------------------------
bool FEPeriodicBoundary2O::Augment(int naug)
{
	// make sure we need to augment
	if (!m_blaugon) return true;

	int i;

	double g;
	vec3d lm;

	// calculate initial norms
	double normL0 = 0;
	for (i=0; i<m_ss.Nodes(); ++i)
	{
		lm = m_ss.m_Lm[i];
		normL0 += lm*lm;
	}
	for (i=0; i<m_ms.Nodes(); ++i)
	{
		lm = m_ms.m_Lm[i];
		normL0 += lm*lm;
	}
	normL0 = sqrt(normL0);

	// update Lagrange multipliers and calculate current norms
	double normL1 = 0;
	double normgc = 0;
	int N = 0;
	for (i=0; i<m_ss.Nodes(); ++i)
	{
		lm = m_ss.m_Lm[i] + m_ss.m_gap[i]*m_eps;

		normL1 += lm*lm;
		g = m_ss.m_gap[i].norm();
		normgc += g*g;
		++N;
	}
	for (i=0; i<m_ms.Nodes(); ++i)
	{
		lm = m_ms.m_Lm[i] + m_ms.m_gap[i]*m_eps;

		normL1 += lm*lm;
		g = m_ms.m_gap[i].norm();
		normgc += g*g;
		++N;
	}
	if (N == 0) N=1;

	normL1 = sqrt(normL1);
	normgc = sqrt(normgc / N);

	felog.printf(" tied interface # %d\n", GetID());
	felog.printf("                        CURRENT        REQUIRED\n");
	double pctn = 0;
	if (fabs(normL1) > 1e-10) pctn = fabs((normL1 - normL0)/normL1);
	felog.printf("    normal force : %15le %15le\n", pctn, m_atol);
	felog.printf("    gap function : %15le       ***\n", normgc);

	// check convergence of constraints
	bool bconv = true;
	if (pctn >= m_atol) bconv = false;
	if (m_naugmin > naug) bconv = false;

	// update Lagrange multipliers if we did not converge
	if (bconv == false)
	{
		for (i=0; i<m_ss.Nodes(); ++i)
		{
			// update Lagrange multipliers
			m_ss.m_Lm[i] = m_ss.m_Lm[i] + m_ss.m_gap[i]*m_eps;
		}
		for (i=0; i<m_ms.Nodes(); ++i)
		{
			// update Lagrange multipliers
			m_ms.m_Lm[i] = m_ms.m_Lm[i] + m_ms.m_gap[i]*m_eps;
		}
	}

	return bconv;
}

//-----------------------------------------------------------------------------
void FEPeriodicBoundary2O::Serialize(DumpFile &ar)
{
	// store contact data
	FEContactInterface::Serialize(ar);

	// store contact surface data
	m_ms.Serialize(ar);
	m_ss.Serialize(ar);
}
