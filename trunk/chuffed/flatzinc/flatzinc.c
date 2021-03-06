/*
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <chuffed/support/vec.h>
#include <chuffed/flatzinc/flatzinc.h>
#include <chuffed/core/engine.h>
#include <chuffed/branching/branching.h>


using namespace std;

void output_var(Branching *v);

namespace FlatZinc {

	FlatZincSpace *s;

	VarBranch ann2ivarsel(AST::Node* ann) {
		if (AST::Atom* s = dynamic_cast<AST::Atom*>(ann)) {
			if (s->id == "input_order") return VAR_INORDER;
			if (s->id == "first_fail") return VAR_SIZE_MIN;
			if (s->id == "anti_first_fail") return VAR_SIZE_MAX;
			if (s->id == "smallest") return VAR_MIN_MIN;
			if (s->id == "largest") return VAR_MAX_MAX;
			if (s->id == "occurrence") return VAR_DEGREE_MAX;
			if (s->id == "most_constrained") return VAR_SIZE_MIN;
			if (s->id == "max_regret") return VAR_REGRET_MIN_MAX;
			if (s->id == "random") return VAR_RANDOM;
		}
		std::cerr << "% Warning, ignored search annotation: ";
		ann->print(std::cerr);
		std::cerr << std::endl;
		return VAR_INORDER;
	}

	ValBranch ann2ivalsel(AST::Node* ann) {
		if (AST::Atom* s = dynamic_cast<AST::Atom*>(ann)) {
			if (s->id == "default") return VAL_DEFAULT;
			if (s->id == "indomain") return VAL_MIN;
			if (s->id == "indomain_min") return VAL_MIN;
			if (s->id == "indomain_max") return VAL_MAX;
			if (s->id == "indomain_middle") return VAL_MIDDLE;
			if (s->id == "indomain_median") return VAL_MEDIAN;
			if (s->id == "indomain_split") return VAL_SPLIT_MIN;
			if (s->id == "indomain_reverse_split") return VAL_SPLIT_MAX;
			if (s->id == "indomain_random") return VAL_RANDOM;
		}
		std::cerr << "% Warning, ignored search annotation: ";
		ann->print(std::cerr);
		std::cerr << std::endl;
		return VAL_DEFAULT;
	}

  FlatZincSpace::FlatZincSpace(int intVars, int boolVars, int setVars) :
		intVarCount(0), boolVarCount(0), iv(intVars), iv_introduced(intVars),
    bv(boolVars), bv_introduced(boolVars), output(NULL) { s = this; }

	void FlatZincSpace::newIntVar(IntVarSpec* vs) {
		if (vs->alias) {
			iv[intVarCount++] = iv[vs->i];
		} else {
			IntVar* v = NULL;
			if (vs->assigned) v = getConstant(vs->i);
			else if (vs->domain()) {
				AST::SetLit* sl = vs->domain.some();
				if (sl->interval) v = ::newIntVar(sl->min, sl->max);
				else {
					vec<int> d;
					for (unsigned int i = 0; i < sl->s.size(); i++) d.push(sl->s[i]);
					sort((int*) d, (int*) d + d.size());
					v = ::newIntVar(d[0], d.last());
					if (!v->allowSet(d)) TL_FAIL();
				}
			} else {
				v = ::newIntVar();
			}
			iv[intVarCount++] = v;
		}
		iv_introduced[intVarCount-1] = vs->introduced;
	}

	void FlatZincSpace::newBoolVar(BoolVarSpec* vs) {
		if (vs->alias) {
			bv[boolVarCount++] = bv[vs->i];
		} else {
			BoolView v;
#if EXPOSE_INT_LITS
			if (vs->alias_var >= 0) {
				iv[vs->alias_var]->specialiseToEL();
				switch (vs->alias_irt) {
				case IRT_EQ:
					v = BoolView(iv[vs->alias_var]->getLit(vs->alias_val, 1));
					break;
				case IRT_NE:
					v = BoolView(iv[vs->alias_var]->getLit(vs->alias_val, 0));
					break;
				case IRT_GE:
					v = BoolView(iv[vs->alias_var]->getLit(vs->alias_val, 2));
					break;
				case IRT_GT:
					v = BoolView(iv[vs->alias_var]->getLit(vs->alias_val + 1, 2));
					break;
				case IRT_LE:
					v = BoolView(iv[vs->alias_var]->getLit(vs->alias_val, 3));
					break;
				case IRT_LT:
					v = BoolView(iv[vs->alias_var]->getLit(vs->alias_val - 1, 3));
					break;
				default:
					NEVER;
				}
			}
			else
#endif
				v = ::newBoolVar();
			if (vs->assigned) v.setVal(vs->i);
			else if (vs->domain()) {
				AST::SetLit* sl = vs->domain.some();
				assert(sl->interval);
				assert(sl->min <= 1);
				assert(sl->max >= 0);
				assert(sl->min <= sl->max);
				if (sl->min == 1) v.setVal(true);
				if (sl->max == 0) v.setVal(false);
			}
			bv[boolVarCount++] = v;
		}
		bv_introduced[boolVarCount-1] = vs->introduced;
	}

  void
  FlatZincSpace::newSetVar(SetVarSpec*) {
    throw FlatZinc::Error("LazyGeoff", "set variables not supported");
  }

	void FlatZincSpace::postConstraint(const ConExpr& ce, AST::Node* ann) {
		try {
			registry().post(ce, ann);
		} catch (AST::TypeError& e) {
			throw FlatZinc::Error("Type error", e.what());
		} catch (exception& e) {
			throw FlatZinc::Error("LazyGeoff", e.what());
		}
	}

	void flattenAnnotations(AST::Array* ann, std::vector<AST::Node*>& out) {
		for (unsigned int i=0; i<ann->a.size(); i++) {
			if (ann->a[i]->isCall("seq_search")) {
				AST::Call* c = ann->a[i]->getCall();
				if (c->args->isArray())
					flattenAnnotations(c->args->getArray(), out);
				else
					out.push_back(c->args);
			} else {
				out.push_back(ann->a[i]);
			}
		}
	}

	// Users should add search annotation with (core vars, default, default) even if they know nothing

	void FlatZincSpace::parseSolveAnn(AST::Array* ann) {
		bool hadSearchAnnotation = false;
		if (ann) {
			std::vector<AST::Node*> flatAnn;
			flattenAnnotations(ann, flatAnn);
			for (unsigned int i=0; i<flatAnn.size(); i++) {
				try {
					AST::Call *call = flatAnn[i]->getCall("int_search");
					AST::Array *args = call->getArgs(4);
					AST::Array *vars = args->a[0]->getArray();
					vec<Branching*> va;
					for (unsigned int i = 0; i < vars->a.size(); i++) {
						if (vars->a[i]->isInt()) continue;
						IntVar* v = iv[vars->a[i]->getIntVar()];
						if (v->isFixed()) continue;
						va.push(v);
					}
					branch(va, ann2ivarsel(args->a[1]), ann2ivalsel(args->a[2]));
					if (AST::String* s = dynamic_cast<AST::String*>(args->a[3])) {
						if (s->s == "all") so.nof_solutions = 0;
					}
					hadSearchAnnotation = true;
				} catch (AST::TypeError& e) {
					(void) e;
					try {
						AST::Call *call = flatAnn[i]->getCall("bool_search");
						AST::Array *args = call->getArgs(4);
						AST::Array *vars = args->a[0]->getArray();
						vec<Branching*> va(vars->a.size());
						for (int i=vars->a.size(); i--; )
							va[i] = new BoolView(bv[vars->a[i]->getBoolVar()]);
						branch(va, ann2ivarsel(args->a[1]), ann2ivalsel(args->a[2]));        
						if (AST::String* s = dynamic_cast<AST::String*>(args->a[3])) {
							if (s->s == "all") so.nof_solutions = 0;
						}
						hadSearchAnnotation = true;
					} catch (AST::TypeError& e) {
						(void) e;
						fprintf(stderr, "%% Type error in search annotation. Ignoring!\n");
					}
				}
			}
		} 
		if (!hadSearchAnnotation) {
			fprintf(stderr, "%% No search annotation given. Defaulting to VSIDS!\n");
			if (!so.vsids) {
				so.vsids = true;
				engine.branching->add(&sat);
			}
		}
	}

	void FlatZincSpace::fixAllSearch() {
		vec<Branching*> va;
		for (int i = 0; i < iv.size(); i++) {
			if (iv_introduced[i]) continue;
			IntVar* v = iv[i];
			if (v->isFixed()) continue;
			va.push(v);
		}
		for (int i = 0; i < bv.size(); i++) {
			if (bv_introduced[i]) continue;
			va.push(new BoolView(bv[i]));
		}
		for (int i = iv.size(); i--; ) {
			if (!iv_introduced[i]) continue;
			IntVar* v = iv[i];
			if (v->isFixed()) continue;
			va.push(v);
		}
		for (int i = 0; i < bv.size(); i++) {
			if (!bv_introduced[i]) continue;
			va.push(new BoolView(bv[i]));
		}
		if (va.size()) branch(va, VAR_INORDER, VAL_DEFAULT);
	}

	void FlatZincSpace::solve(AST::Array* ann) {
		parseSolveAnn(ann);
		fixAllSearch();
	}

	void FlatZincSpace::minimize(int var, AST::Array* ann) {
		parseSolveAnn(ann);
		optimize(iv[var], OPT_MIN);
		fixAllSearch();		
	}

	void FlatZincSpace::maximize(int var, AST::Array* ann) {
		parseSolveAnn(ann);
		optimize(iv[var], OPT_MAX);
		fixAllSearch();
	}

	void FlatZincSpace::setOutputElem(AST::Node* ai) const {
		if (ai->isIntVar()) {
			output_var(iv[ai->getIntVar()]);
		} else if (ai->isBoolVar()) {
			output_var(new BoolView(bv[ai->getBoolVar()]));
		}
	}

	void FlatZincSpace::setOutput() {
		if (output == NULL) return;
		for (unsigned int i=0; i< output->a.size(); i++) {
			AST::Node* ai = output->a[i];
			if (ai->isArray()) {
				AST::Array* aia = ai->getArray();
				int size = aia->a.size();
				for (int j=0; j<size; j++) {
					setOutputElem(aia->a[j]);
				}
			} else if (ai->isCall("ifthenelse")) {
				AST::Array* aia = ai->getCall("ifthenelse")->getArgs(3);
				setOutputElem(aia->a[1]);
				setOutputElem(aia->a[2]);
			} else {
				setOutputElem(ai);
			}
		}
	}

	void FlatZincSpace::printElem(AST::Node* ai) const {
		int k;
		if (ai->isInt(k)) {
			cout << k;
		} else if (ai->isIntVar()) {
			cout << iv[ai->getIntVar()]->getVal();
		} else if (ai->isBoolVar()) {
			if (bv[ai->getBoolVar()].isTrue()) {
				cout << "true";
			} else if (bv[ai->getBoolVar()].isFalse()) {
				cout << "false";
			} else {
				cout << "false..true";
			}
		} else if (ai->isBool()) {
			cout << (ai->getBool() ? "true" : "false");
		} else if (ai->isSet()) {
			AST::SetLit* s = ai->getSet();
			if (s->interval) {
				cout << s->min << ".." << s->max;
			} else {
				cout << "{";
				for (unsigned int i=0; i<s->s.size(); i++) {
					cout << s->s[i] << (i < s->s.size()-1 ? ", " : "}");
				}
			}
		} else if (ai->isString()) {
			std::string s = ai->getString();
			for (unsigned int i=0; i<s.size(); i++) {
				if (s[i] == '\\' && i<s.size()-1) {
					switch (s[i+1]) {
					case 'n': cout << "\n"; break;
					case '\\': cout << "\\"; break;
					case 't': cout << "\t"; break;
					default: cout << "\\" << s[i+1];
					}
					i++;
				} else {
					cout << s[i];
				}
			}
		}
	}

	void FlatZincSpace::print() {
		if (output == NULL) return;
		for (unsigned int i=0; i< output->a.size(); i++) {
			AST::Node* ai = output->a[i];
			if (ai->isArray()) {
				AST::Array* aia = ai->getArray();
				int size = aia->a.size();
				std::cout << "[";
				for (int j=0; j<size; j++) {
					printElem(aia->a[j]);
					if (j<size-1) cout << ", ";
				}
				std::cout << "]";
			} else if (ai->isCall("ifthenelse")) {
				AST::Array* aia = ai->getCall("ifthenelse")->getArgs(3);
				if (aia->a[0]->isBool()) {
					if (aia->a[0]->getBool()) printElem(aia->a[1]);
					else printElem(aia->a[2]);
				} else if (aia->a[0]->isBoolVar()) {
					BoolView b = bv[aia->a[0]->getBoolVar()];
					if (b.isTrue()) printElem(aia->a[1]);
					else if (b.isFalse()) printElem(aia->a[2]);
					else std::cerr << "% Error: Condition not fixed." << std::endl;
				} else {
					std::cerr << "% Error: Condition not Boolean." << std::endl;        
				}
			} else {
				printElem(ai);
			}
		}
	}

}
