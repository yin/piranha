#include "../include/ir_binary_operator.h"

#include "../include/ir_node_definition.h"
#include "../include/ir_attribute_list.h"
#include "../include/ir_attribute.h"
#include "../include/ir_value_constant.h"
#include "../include/ir_attribute_definition_list.h"
#include "../include/node.h"

piranha::IrBinaryOperator::IrBinaryOperator(OPERATOR op, IrValue *left, IrValue *right) :
														IrValue(IrValue::BINARY_OPERATION) {
	m_operator = op;
	m_leftOperand = left;
	m_rightOperand = right;

	registerComponent(left);
	registerComponent(right);

	// The data access operators are special in the sense that the right-hand operand
	// will *always* be a standard label that doesn't reference anything itself.
	// Therefore it wouldn't be wise to resolve the reference of that label
	if (op == DOT || op == POINTER) {
		if (m_rightOperand != nullptr) {
			m_rightOperand->setCheckReferences(false);
		}
	}
}

piranha::IrBinaryOperator::~IrBinaryOperator() {
	/* void */
}

piranha::IrParserStructure *piranha::IrBinaryOperator::resolveLocalName(const std::string &name) const {
	return nullptr;
}

piranha::IrParserStructure *piranha::IrBinaryOperator::getImmediateReference(
					const IrReferenceQuery &query, IrReferenceInfo *output) {
	IR_RESET(query);
	
	if (m_leftOperand == nullptr || m_rightOperand == nullptr) {
		// There was a syntax error so this step can be skipped
		IR_FAIL();
		return nullptr;
	}

	// The dot is the reference operator
	if (m_operator == DOT || m_operator == POINTER) {
		IrReferenceQuery basicQuery;
		basicQuery.inputContext = query.inputContext;
		basicQuery.recordErrors = false;
		IrReferenceInfo basicInfo;
		IrParserStructure *resolvedLeft = m_leftOperand->getReference(basicQuery, &basicInfo);

		if (basicInfo.failed) {
			IR_FAIL();
			return nullptr;
		}

		if (basicInfo.reachedDeadEnd) {
			IR_DEAD_END();
			return nullptr;
		}

		bool touchedMainContext = false;
		if (basicInfo.touchedMainContext) {
			IR_INFO_OUT(touchedMainContext, true);
			touchedMainContext = true;
		}

		IrContextTree *currentContext = basicInfo.newContext;

		// Perform code specific to the pointer operator
		if (m_operator == POINTER) {
			IrNode *asNode = resolvedLeft->getAsNode();
			IrValue *asValue = resolvedLeft->getAsValue();

			if (asValue != nullptr) resolvedLeft = asValue;
			else if (asNode != nullptr) resolvedLeft = asNode->getDefaultOutputValue();

			if (resolvedLeft == nullptr) {
				IR_FAIL();

				bool isValidError = (IR_EMPTY_CONTEXT() || touchedMainContext);
				if (query.recordErrors && isValidError) {
					IR_ERR_OUT(new CompilationError(*m_leftOperand->getSummaryToken(),
						ErrorCode::CannotFindDefaultValue, query.inputContext));
				}

				return nullptr;
			}

			if (currentContext != nullptr) {
				currentContext = currentContext->newChild(asNode);
			}

			IrReferenceInfo refInfo;
			IrReferenceQuery refQuery;
			refQuery.inputContext = currentContext;
			refQuery.recordErrors = false;
			resolvedLeft = resolvedLeft->getReference(refQuery, &refInfo);

			if (IR_FAILED(&refInfo)) {
				IR_FAIL();
				return nullptr;
			}

			if (refInfo.reachedDeadEnd) {
				IR_DEAD_END();
				return nullptr;
			}

			if (refInfo.touchedMainContext) {
				IR_INFO_OUT(touchedMainContext, true);
				touchedMainContext = true;
			}

			currentContext = refInfo.newContext;
		}

		IrValueLabel *labelConstant = static_cast<IrValueLabel *>(m_rightOperand);
		IrParserStructure *publicAttribute = resolvedLeft->resolveLocalName(labelConstant->getValue());

		if (publicAttribute == nullptr) {
			IR_FAIL();

			bool isValidError = (IR_EMPTY_CONTEXT() || touchedMainContext);
			if (query.recordErrors && isValidError) {
				// Left hand does not have this member
				IR_ERR_OUT(new CompilationError(*m_rightOperand->getSummaryToken(),
					ErrorCode::UndefinedMember, query.inputContext));
			}

			return nullptr;
		}

		// Check to make sure that the user is not accidentally trying to use a hidden member
		if (!publicAttribute->allowsExternalAccess()) {
			IR_FAIL();

			bool isValidError = (IR_EMPTY_CONTEXT() || touchedMainContext);
			if (query.recordErrors && isValidError) {
				IR_ERR_OUT(new CompilationError(*m_rightOperand->getSummaryToken(),
					ErrorCode::AccessingInternalMember, query.inputContext));
			}

			return nullptr;
		}

		if (currentContext != nullptr) {
			IR_INFO_OUT(newContext, currentContext->newChild(resolvedLeft->getAsNode()));
		}

		return publicAttribute;
	}
	else {
		IrNode **pNode = m_expansions.lookup(query.inputContext);

		if (pNode == nullptr) {
			IR_DEAD_END();
			return nullptr;
		}
		else if (*pNode == nullptr) {
			IR_FAIL();
			return nullptr;
		}
		else {
			return *pNode;
		}
	}
}

void piranha::IrBinaryOperator::_expand(IrContextTree *context) {
	if (m_leftOperand == nullptr || m_rightOperand == nullptr) return;

	if (m_operator != DOT && m_operator != POINTER) {
		if (m_rules == nullptr) return;

		IrReferenceInfo leftInfo;
		IrReferenceQuery leftQuery;
		leftQuery.inputContext = context;
		leftQuery.recordErrors = false;
		IrParserStructure *leftReference = m_leftOperand->resolveToSingleChannel(leftQuery, &leftInfo);

		if (leftInfo.failed) return;
		if (leftInfo.reachedDeadEnd) return;

		IrReferenceInfo rightInfo;
		IrReferenceQuery rightQuery;
		rightQuery.inputContext = context;
		rightQuery.recordErrors = false;
		IrParserStructure *rightReference = m_rightOperand->resolveToSingleChannel(rightQuery, &rightInfo);

		if (rightInfo.failed) return;
		if (rightInfo.reachedDeadEnd) return;

		const ChannelType *leftType = leftReference->getImmediateChannelType();
		const ChannelType *rightType = rightReference->getImmediateChannelType();

		std::string builtinType = m_rules->resolveOperatorBuiltinType(m_operator, leftType, rightType);

		if (builtinType.empty()) {
			getParentUnit()->addCompilationError(
				new CompilationError(m_summaryToken, ErrorCode::InvalidOperandTypes, context)
			);
			return;
		}

		int count = 0;
		IrNodeDefinition *nodeDefinition = getParentUnit()->resolveBuiltinNodeDefinition(builtinType, &count);

		IrNode *expansion = new IrNode();
		expansion->setLogicalParent(this);
		expansion->setScopeParent(this);
		expansion->setDefinition(nodeDefinition);
		expansion->setRules(m_rules);

		*m_expansions.newValue(context) = expansion;
	}
}

piranha::NodeOutput *piranha::IrBinaryOperator::_generateNodeOutput(IrContextTree *context, NodeProgram *program) {
	if (m_leftOperand == nullptr || m_rightOperand == nullptr) {
		// There was a syntax error so this step can be skipped
		return nullptr;
	}

	// The dot is the reference operator
	if (m_operator == DOT || m_operator == POINTER) {
		IrValue *value = m_leftOperand;

		IrValueLabel *labelConstant = static_cast<IrValueLabel *>(m_rightOperand);

		const NodeOutput *leftOutput = m_leftOperand->generateNodeOutput(context, program);
		const Node *leftNode = m_leftOperand->generateNode(context, program);

		if (leftOutput == nullptr && leftNode == nullptr) return nullptr;
		if (leftNode != nullptr) {
			if (m_operator == DOT) {
				return leftNode->getOutput(labelConstant->getValue().c_str());
			}
			else if (m_operator == POINTER) {
				NodeOutput *primaryOutput = leftNode->getPrimaryOutput();
				Node *outputInterface = primaryOutput->getInterface();
				NodeOutput *output = outputInterface->getOutput(labelConstant->getValue().c_str());
				return leftNode
					->getPrimaryOutput()
					->getInterface()
					->getOutput(labelConstant->getValue().c_str());
			}
		}

		if (leftOutput != nullptr) {
			if (m_operator == DOT) {
				return leftOutput
					->getInterface()
					->getOutput(labelConstant->getValue().c_str());
			}
			else if (m_operator == POINTER) {
				return leftOutput
					->getInterface()
					->getPrimaryOutput()
					->getInterface()
					->getOutput(labelConstant->getValue().c_str());
			}
		}
	}

	// TODO: add other operators
	return nullptr;
}

piranha::Node *piranha::IrBinaryOperator::_generateNode(IrContextTree *context, NodeProgram *program) {
	if (m_leftOperand == nullptr || m_rightOperand == nullptr) {
		// There was a syntax error so this step can be skipped
		return nullptr;
	}

	// The dot is the reference operator
	if (m_operator == DOT || m_operator == POINTER) {
		IrReferenceInfo info;
		IrReferenceQuery query;
		query.inputContext = context;
		query.recordErrors = false;

		IrParserStructure *reference = getReference(query, &info);
		return reference->generateNode(info.newContext, program);
	}
	else {
		return program->getRules()->generateOperatorNode(this, context);
	}
}
