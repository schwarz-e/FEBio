#include "stdafx.h"
#include "MathObject.h"
#include "MMath.h"
#include "MObjBuilder.h"
#include <float.h>
using namespace std;

//-----------------------------------------------------------------------------
MathObject::MathObject()
{

}

MathObject::MathObject(const MathObject& mo)
{
	// copy variable list
	for (int i = 0; i < mo.Variables(); ++i)
	{
		AddVariable(new MVariable(*mo.Variable(i)));
	}
}

//-----------------------------------------------------------------------------
void MathObject::operator = (const MathObject& mo)
{
	// copy variable list
	for (int i = 0; i < mo.Variables(); ++i)
	{
		AddVariable(new MVariable(*mo.Variable(i)));
	}
}

//-----------------------------------------------------------------------------
MathObject::~MathObject()
{
	for (int i = 0; i < m_Var.size(); ++i) delete m_Var[i];
	m_Var.clear();
}

//-----------------------------------------------------------------------------
MVariable* MathObject::AddVariable(const std::string& var)
{
	if (m_Var.empty() == false)
	{
		MVarList::iterator it = m_Var.begin();
		for (it = m_Var.end(); it != m_Var.end(); ++it)
			if ((*it)->Name() == var) return *it;
	}

	MVariable* pv = new MVariable(var);
	pv->setIndex((int)m_Var.size());
	m_Var.push_back(pv);
	return pv;
}

void MathObject::AddVariable(MVariable* pv)
{
	if (m_Var.empty() == false)
	{
		MVarList::iterator it = m_Var.begin();
		for (it = m_Var.end(); it != m_Var.end(); ++it)
			if (*it == pv) return;
	}
	pv->setIndex((int)m_Var.size());
	m_Var.push_back(pv);
}

//-----------------------------------------------------------------------------
MVariable* MathObject::FindVariable(const string& s)
{
	for (MVarList::iterator it = m_Var.begin(); it != m_Var.end(); ++it)
		if ((*it)->Name() == s) return *it;
	return 0;
}

//-----------------------------------------------------------------------------
int MSimpleExpression::Items()
{
	if (m_item.Type() == MSEQUENCE)
	{
		MSequence* pe = dynamic_cast<MSequence*>(m_item.ItemPtr());
		return pe->size();
	}
	else return 1;
}

//-----------------------------------------------------------------------------
double MSimpleExpression::value(const MItem* pi)
{
	switch (pi->Type())
	{
	case MCONST: return (mnumber(pi)->value());
	case MFRAC : return (mnumber(pi)->value());
	case MNAMED: return (mnumber(pi)->value());
	case MVAR  : return (mnumber(pi)->value());
	case MNEG: return -value(munary(pi)->Item());
	case MADD: return value(mbinary(pi)->LeftItem()) + value(mbinary(pi)->RightItem());
	case MSUB: return value(mbinary(pi)->LeftItem()) - value(mbinary(pi)->RightItem());
	case MMUL: return value(mbinary(pi)->LeftItem()) * value(mbinary(pi)->RightItem());
	case MDIV: return value(mbinary(pi)->LeftItem()) / value(mbinary(pi)->RightItem());
	case MPOW: return pow(value(mbinary(pi)->LeftItem()), value(mbinary(pi)->RightItem()));
	case MF1D:
		{
			double a = value(munary(pi)->Item());
			return (mfnc1d(pi)->funcptr())(a);
		}
		break;
	case MF2D:
		{
			double a = value(mbinary(pi)->LeftItem());
			double b = value(mbinary(pi)->RightItem());
			return (mfnc2d(pi)->funcptr())(a, b);
		}
		break;
	case MSFNC:
		{
			return value(msfncnd(pi)->Value());
		};		
	default:
		assert(false);
		return 0;
	}
}

//-----------------------------------------------------------------------------
double MSimpleExpression::value(const MItem* pi, const std::vector<double>& var)
{
	switch (pi->Type())
	{
	case MCONST: return (mnumber(pi)->value());
	case MFRAC : return (mnumber(pi)->value());
	case MNAMED: return (mnumber(pi)->value());
	case MVAR  : return (var[mvar(pi)->index()]);
	case MNEG: return -value(munary(pi)->Item(), var);
	case MADD: return value(mbinary(pi)->LeftItem(), var) + value(mbinary(pi)->RightItem(), var);
	case MSUB: return value(mbinary(pi)->LeftItem(), var) - value(mbinary(pi)->RightItem(), var);
	case MMUL: return value(mbinary(pi)->LeftItem(), var) * value(mbinary(pi)->RightItem(), var);
	case MDIV: return value(mbinary(pi)->LeftItem(), var) / value(mbinary(pi)->RightItem(), var);
	case MPOW: return pow(value(mbinary(pi)->LeftItem(), var), value(mbinary(pi)->RightItem(), var));
	case MF1D:
		{
			double a = value(munary(pi)->Item(), var);
			return (mfnc1d(pi)->funcptr())(a);
		}
		break;
	case MF2D:
		{
			double a = value(mbinary(pi)->LeftItem(), var);
			double b = value(mbinary(pi)->RightItem(), var);
			return (mfnc2d(pi)->funcptr())(a, b);
		}
		break;
	case MSFNC:
		{
			return value(msfncnd(pi)->Value(), var);
		};		
	default:
		assert(false);
		return 0;
	}
}

//-----------------------------------------------------------------------------
MSimpleExpression::MSimpleExpression(const MSimpleExpression& mo) : MathObject(mo), m_item(mo.m_item)
{
	// The copy c'tor of MathObject copied the variables, but any MVarRefs still point to the mo object, not this object's var list.
	// Calling the following function fixes this
	fixVariableRefs(m_item.ItemPtr());
}

//-----------------------------------------------------------------------------
void MSimpleExpression::operator=(const MSimpleExpression& mo)
{
	// copy base object
	MathObject::operator=(mo);

	// copy the item
	m_item = mo.m_item;

	// The = operator of MathObject copied the variables, but any MVarRefs still point to the mo object, not this object's var list.
	// Calling the following function fixes this
	fixVariableRefs(m_item.ItemPtr());
}

//-----------------------------------------------------------------------------
void MSimpleExpression::fixVariableRefs(MItem* pi)
{
	if (pi->Type() == MVAR)
	{
		MVarRef* var = static_cast<MVarRef*>(pi);
		int index = var->GetVariable()->index();
		var->SetVariable(Variable(index));
	}
	else if (is_unary(pi))
	{
		MUnary* uno = static_cast<MUnary*>(pi);
		fixVariableRefs(uno->Item());
	}
	else if (is_binary(pi))
	{
		MBinary* bin = static_cast<MBinary*>(pi);
		fixVariableRefs(bin->LeftItem());
		fixVariableRefs(bin->RightItem());
	}
	else if (is_nary(pi))
	{
		MNary* any = static_cast<MNary*>(pi);
		for (int i = 0; i < any->Params(); ++i) fixVariableRefs(any->Param(i));
	}
}

//-----------------------------------------------------------------------------
// Create a simple expression object from a string
bool MSimpleExpression::Create(const std::string& expr, bool autoVars)
{
	MObjBuilder mob;
	mob.setAutoVars(autoVars);
	if (mob.Create(this, expr, true) == false) return false;
	return true;
}