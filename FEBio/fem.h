// fem.h: interface for the FEM class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _FEM_H_07012006_
#define _FEM_H_07012006_

#include "PlotFile.h"
#include "LoadCurve.h"
#include "FEElementLibrary.h"
#include "DumpFile.h"
#include "FEMesh.h"
#include "FEContactInterface.h"
#include "FEMaterial.h"
#include "FERigidBody.h"
#include "FESolver.h"
#include "DataStore.h"
#include "FERigidJoint.h"
#include "FEAnalysis.h"
#include "FEAugLagLinearConstraint.h"
#include "Timer.h"
#include "FEPressureSurface.h"
#include "FEPoroTraction.h"
#include "FEFluidFluxSurface.h"
#include "FESoluteFluxSurface.h"
#include "FEHeatFlux.h"
#include "FEDiscreteMaterial.h"
#include "FEBodyForce.h"

#include <stack>
#include <list>
using namespace std;

#define MAX_STRING	256

//-----------------------------------------------------------------------------
//! A degree of freedom structure

class DOF
{
public:
	DOF() { node = bc = neq = -1; }
public:
	int	node;	// the node to which this dof belongs to
	int	bc;		// the degree of freedom
	int	neq;	// the equation number (or -1 if none)
};

//-----------------------------------------------------------------------------
//! linear constraint

class FELinearConstraint
{
public:
	class SlaveDOF : public DOF
	{
	public:
		SlaveDOF() : val(0){}
		double	val;	// coefficient value
	};

public:
	FELinearConstraint(){}
	FELinearConstraint(const FELinearConstraint& LC)
	{
		master = LC.master;
		int n = (int) LC.slave.size();
		list<SlaveDOF>::const_iterator it = LC.slave.begin();
		for (int i=0; i<n; ++i) slave.push_back(*it);
	}

	double FindDOF(int n)
	{
		int N = slave.size();
		list<SlaveDOF>::iterator it = slave.begin();
		for (int i=0; i<N; ++i, ++it) if (it->neq == n) return it->val;

		return 0;
	}

	void Serialize(DumpFile& ar)
	{
		if (ar.IsSaving())
		{
			ar.write(&master, sizeof(DOF), 1);
			int n = (int) slave.size();
			ar << n;
			list<SlaveDOF>::iterator it = slave.begin();
			for (int i=0; i<n; ++i, ++it) ar << it->val << it->node << it->bc << it->neq;
		}
		else
		{
			slave.clear();
			ar.read(&master, sizeof(DOF), 1);
			int n;
			ar >> n;
			for (int i=0; i<n; ++i)
			{
				SlaveDOF dof;
				ar >> dof.val >> dof.node >> dof.bc >> dof.neq;
				slave.push_back(dof);
			}
		}
	}

public:
	DOF			master;	// master degree of freedom
	list<SlaveDOF>	slave;	// list of slave nodes
};

//-----------------------------------------------------------------------------
//! concentrated nodal force boundary condition

class FENodalForce : public FEBoundaryCondition
{
public:
	double	s;		// scale factor
	int		node;	// node number
	int		bc;		// force direction
	int		lc;		// load curve
};

//-----------------------------------------------------------------------------
//! prescribed nodal displacement data

class FENodalDisplacement : public FEBoundaryCondition
{
public:
	double	s;		// scale factor
	int		node;	// node number
	int		bc;		// displacement direction
	int		lc;		// load curve
};

//-----------------------------------------------------------------------------
//! rigid node

class FERigidNode : public FEBoundaryCondition
{
public:
	int	nid;	// node number
	int	rid;	// rigid body number
};

//-----------------------------------------------------------------------------
// forward declaration of FESolidSolver class
class FESolidSolver;

//-----------------------------------------------------------------------------
// forward declaration of the FEM class
class FEM;

//-----------------------------------------------------------------------------
// FEBIO callback structure
typedef void (*FEBIO_CB_FNC)(FEM*,void*);
struct FEBIO_CALLBACK {
	FEBIO_CB_FNC	m_pcb;
	void*	m_pd;
};

//-----------------------------------------------------------------------------
//! The Finite Element Model class. 

//! This class stores solver parameters, geometry data, material data, and 
//! other data that is needed to solve the FE problem.
//! FEBio is designed to solve finite element problems. All the finite element
//! data is collected here in this class. This class also provides
//! routines to initalize, input, output and update the FE data. Although this
//! class provides the main solve routine it does not really solve anything.
//! The actual solving is done by the FESolidSolver class.

class FEM
{
public:
	//! constructor - sets default variables
	FEM();

	//! destructor
	virtual ~FEM();

	//! read the configuration file
	bool Configure(const char* szfile);

	//! Restart from restart point
	bool Restart(const char* szfile);

	//! Initializes data structures
	bool Init();

	//! check data
	bool Check();

	//! Resets data structures
	bool Reset();

	//! Solves the problem
	bool Solve();

	//! Serialize the current state to/from restart file
	bool Serialize(DumpFile& ar);

	//! input data from file
	bool Input(const char* szfile);

	//! Add a material to the model
	void AddMaterial(FEMaterial* pm) { m_MAT.push_back(pm); }

	//! Add a parameter list
	void AddParameterList(FEParameterList* pl) { m_MPL.push_back(pl); }

	//! get the number of materials
	int Materials() { return m_MAT.size(); }

	//! return a pointer to a material
	FEMaterial* GetMaterial(int id) { return m_MAT[id]; }

	//! return the elastic material
	FEElasticMaterial* GetElasticMaterial(int id)
	{
		FEMaterial* pm = m_MAT[id];
		while (dynamic_cast<FENestedMaterial*>(pm)) pm = (dynamic_cast<FENestedMaterial*>(pm))->m_pBase;
		while (dynamic_cast<FEBiphasic*>(pm)) pm = (dynamic_cast<FEBiphasic*>(pm))->m_pSolid;
		while (dynamic_cast<FEBiphasicSolute*>(pm)) pm = (dynamic_cast<FEBiphasicSolute*>(pm))->m_pSolid;
		FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pm);
		assert(pme);
		return pme;
	}

	//! return the elastic material
	FEElasticMaterial* GetElasticMaterial(FEMaterial* pm)
	{
		while (dynamic_cast<FENestedMaterial*>(pm)) pm = (dynamic_cast<FENestedMaterial*>(pm))->m_pBase;
		while (dynamic_cast<FEBiphasic*>(pm)) pm = (dynamic_cast<FEBiphasic*>(pm))->m_pSolid;
		while (dynamic_cast<FEBiphasicSolute*>(pm)) pm = (dynamic_cast<FEBiphasicSolute*>(pm))->m_pSolid;
		FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pm);
		assert(pme);
		return pme;
	}

	//! Add a loadcurve to the model
	void AddLoadCurve(FELoadCurve* plc) { m_LC.push_back(plc); }

	//! get a loadcurve
	FELoadCurve* GetLoadCurve(int i) { return m_LC[i]; }

	//! get the number of loadcurves
	int LoadCurves() { return m_LC.size(); }

	//! set the debug level
	void SetDebugFlag(bool b) { m_debug = b; }

	//! get the debug level
	bool GetDebugFlag() { return m_debug; }


	// set the i/o files
	void SetInputFilename(const char* szfile)
	{ 
		strcpy(m_szfile, szfile); 
		m_szfile_title = strrchr(m_szfile, '/');
		if (m_szfile_title == 0) 
		{
			m_szfile_title = strchr(m_szfile, '\\'); 
			if (m_szfile_title == 0) m_szfile_title = m_szfile; else ++m_szfile_title;
		}
		else ++m_szfile_title;
	}
	void SetLogFilename  (const char* szfile) { strcpy(m_szlog , szfile); }
	void SetPlotFilename (const char* szfile) { strcpy(m_szplot, szfile); }
	void SetDumpFilename (const char* szfile) { strcpy(m_szdump, szfile); }

	void SetPlotFileNameExtension(const char* szext);

	const char* GetInputFileName () { return m_szfile; }
	const char* GetLogfileName () { return m_szlog; }
	const char* GetPlotFileName() { return m_szplot; }

	//! set the problem title
	void SetTitle(const char* sz) { strcpy(m_sztitle, sz); }

	//! get the problem title
	const char* GetTitle() { return m_sztitle; }

	//! return a pointer to the named variable
	double* FindParameter(const char* szname);

	//! return number of contact interfaces
	int ContactInterfaces() { return m_CI.size(); } 

	//! find a boundary condition from the ID
	FEBoundaryCondition* FindBC(int nid);

	//! Set the sparse matrix symmetry flag
	void SetSymmetryFlag(bool bsymm) { m_bsymm = bsymm; }

public:
	//! copy constructor
	FEM(const FEM& fem);

	//! assignment operator
	void operator = (const FEM& fem);

protected:
	void ShallowCopy(FEM& fem);


protected:
	void SerializeMaterials   (DumpFile& ar);
	void SerializeAnalysisData(DumpFile& ar);
	void SerializeGeometry    (DumpFile& ar);
	void SerializeContactData (DumpFile& ar);
	void SerializeBoundaryData(DumpFile& ar);
	void SerializeIOData      (DumpFile& ar);
	void SerializeLoadData    (DumpFile& ar);

public:
	//{ --- Initialization routines ---

		//! initialze equation numbering
		bool InitEquations();

		//! Initialize rigid bodies
		bool InitRigidBodies();

		//! Initialize poroelastic/biphasic and solute data
		bool InitPoroSolute();

		//! Initializes contact data
		bool InitContact();

		//! Iniatialize linear constraint data
		bool InitConstraints();

		//! Initialize material data
		bool InitMaterials();
	//}

	//{ --- Update routines ---

		//! Update contact data
		void UpdateContact();
	//}

	//{ --- Miscellaneous routines ---

		//! set callback function
		void AddCallback(FEBIO_CB_FNC pcb, void* pd);

		//! call the callback function
		void DoCallback();
	//}

public:
	// --- Analysis Data ---
	//{
		vector<FEAnalysis*>		m_Step;		//!< array of analysis steps
		int						m_nStep;	//!< current analysis step
		FEAnalysis*				m_pStep;	//!< pointer to current analysis step
		double					m_ftime;	//!< current time value
		double					m_ftime0;	//!< start time of current step
		int		m_nhex8;					//!< element type for hex8
		bool	m_b3field;					//!< use three-field implementation 
		bool	m_bsym_poro;		//!< symmetric (old) poro-elastic flag
		int		m_nplane_strain;	//!< run analysis in plain strain mode

		// body force loads
		vector<FEBodyForce*>	m_BF;		//!< body force data

		// Create timer to track total running time
		Timer	m_TotalTime;
	//}

	// --- Geometry Data ---
	//{
		FEMesh	m_mesh;	//!< the FE mesh

		// rigid body data
		int						m_nreq;	//!< start of rigid body equations
		int						m_nrm;	//!< nr of rigid materials
		int						m_nrb;	//!< nr of rigid bodies in problem
		vector<FERigidBody>		m_RB;	//!< rigid body array

		// rigid joints
		int							m_nrj;	//!< nr of rigid joints
		vector<FERigidJoint*>		m_RJ;	//!< rigid joint array

		// discrete elements
		vector<FEDiscreteMaterial*>		m_DMAT;	//!< discrete materials
	//}

	//{ --- Contact Data --
		bool							m_bcontact;	//!< contact flag
		vector<FEContactInterface*>		m_CI;		//!< contact interface array
	//}

protected:
	// --- Material Data ---
	//{
		vector<FEMaterial*>			m_MAT;	//!< array of materials
		vector<FEParameterList*>	m_MPL;	//!< material parameter lists
	//}

	// --- Load Curve Data ---
	//{
		vector<FELoadCurve*>	m_LC;	//!< load curve data
	//}

public:
	// --- Boundary Condition Data ---
	//{
		// displacement boundary data
		vector<FENodalDisplacement*>		m_DC;	//!< prescribed displacement cards

		// concentrated nodal loads data
		vector<FENodalForce*>	m_FC;		//!< concentrated nodal force cards

		// pressure BC
		FEPressureSurface*		m_psurf;	//!< pressure surface domain
		FEConstTractionSurface*	m_ptrac;	//!< constant traction surface

		// normal traction on porous surface BC
		FEPoroTractionSurface*	m_ptsurf;	//!< normal traction surface domain
	
		// fluid flux BC
		FEFluidFluxSurface*		m_fsurf;	//!< fluid flux surface domain

		// solute flux BC
		FESoluteFluxSurface*	m_ssurf;	//!< solute flux surface domain
	
		// heat flux BC
		FEHeatFluxSurface*		m_phflux;	//!< heat flux surface domain

		// rigid displacements
		vector<FERigidBodyDisplacement*>	m_RDC;	//!< rigid body displacements

		// rigid forces
		vector<FERigidBodyForce*>	m_RFC;	//!< rigid body forces

		// rigid nodes
		vector<FERigidNode*>		m_RN;		//!< rigid nodes

		// linear constraint data
		list<FELinearConstraint>	m_LinC;		//!< linear constraints data
		vector<int>					m_LCT;		//!< linear constraint table
		vector<FELinearConstraint*>	m_LCA;		//!< linear constraint array (temporary solution!)

		// Augmented Lagrangian linear constraint data
		list<FELinearConstraintSet*>	m_LCSet;	//!< aug lag linear constraint data
	//}

	// --- Direct Solver Data ---
	//{
		int		m_nsolver;	//!< type of solver selected
		int		m_neq;		//!< number of equations
		int		m_npeq;		//!< number of equations related to pressure dofs
		int		m_nceq;		//!< number of equations related to concentration dofs
		int		m_bwopt;	//!< bandwidth optimization flag
		bool	m_bsymm;	//!< symmetric flag
	//}
 
	// --- I/O-Data --- 
	//{
		PlotFile*	m_plot;		//!< the plot file
		DataStore	m_Data;		//!< the data store used for data logging

protected:
		// file names
		char*	m_szfile_title;			//!< master input file title 
		char	m_szfile[MAX_STRING];	//!< master input file name (= path + title)
		char	m_szplot[MAX_STRING];	//!< plot output file name
		char	m_szlog [MAX_STRING];	//!< log output file name
		char	m_szdump[MAX_STRING];	//!< dump file name

		char	m_sztitle[MAX_STRING];	//!< problem title

		bool	m_debug;	//!< debug flag

		list<FEBIO_CALLBACK>	m_pcb;	//!< pointer to callback function
	//}

	// some friends of this class
	friend class FEAnalysis;
	friend class FESolidSolver;
	friend class stack<FEM>;
};

#endif // _FEM_H_07012006_
