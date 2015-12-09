#pragma once
#include "FEDomain.h"

//-----------------------------------------------------------------------------
//! domain for discrete elements
class FEDiscreteDomain : public FEDomain
{
public:
	FEDiscreteDomain(FEMesh* pm) : FEDomain(FE_DOMAIN_DISCRETE, pm) {}

	void create(int n) { m_Elem.resize(n); }
	int Elements() { return (int) m_Elem.size(); }
	FEElement& ElementRef(int n) { return m_Elem[n]; }

	FEDiscreteElement& Element(int n) { return m_Elem[n]; }

	bool Initialize(FEModel& fem);

	void Activate();

	int Nodes() { return (int) m_Node.size(); }
	FENode& Node(int i);

	//! create a shallow copy
	void ShallowCopy(DumpStream& dmp, bool bsave);

	//! Serialize data to archive
	void Serialize(DumpFile& ar);

public:
	void AddElement(int eid, int n[2]);

protected:
	vector<int>					m_Node;
	vector<FEDiscreteElement>	m_Elem;
};
