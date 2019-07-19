#ifndef PIRANHA_IR_ATTRIBUTE_H
#define PIRANHA_IR_ATTRIBUTE_H

#include "ir_parser_structure.h"

#include "ir_token_info.h"

namespace piranha {

	class IrValue;
	class IrAttributeDefinition;

	class IrAttribute : public IrParserStructure {
	public:
		IrAttribute();
		IrAttribute(const IrTokenInfo_string &name, IrValue *value);
		IrAttribute(IrValue *value);
		~IrAttribute();

		std::string getName() const { return m_name.data; }

		void setValue(IrValue *value);
		IrValue *getValue() const { return m_value; }

		void setPosition(int position) { m_position = position; }
		int getPosition() const { return m_position; }

		// If no name is specified positional notation is assumed
		bool isPositional() const { return getName() == ""; }
		virtual IrValue *getAsValue() { return m_value; }

		virtual bool isExternalInput() const { return true; }

	protected:
		IrTokenInfo_string m_name;
		IrValue *m_value;

		int m_position;

		// Resolution stage
	public:
		void setAttributeDefinition(IrAttributeDefinition *definition) { m_definition = definition; }
		IrAttributeDefinition *getAttributeDefinition() const { return m_definition; }

		virtual IrParserStructure *getImmediateReference(const IrReferenceQuery &query, IrReferenceInfo *output);
		virtual void _checkType(IrParserStructure *finalReference, IrContextTree *context);

	protected:
		IrAttributeDefinition *m_definition;

	protected:
		virtual Node *_generateNode(IrContextTree *context, NodeProgram *program) {
			// TODO: remove?
			IrReferenceInfo info;
			IrReferenceQuery query;
			query.inputContext = context;
			query.recordErrors = false;
			IrParserStructure *reference = getImmediateReference(query, &info);

			if (reference == nullptr) return nullptr;
			else return reference->generateNode(info.newContext, program);
		}

		virtual NodeOutput *_generateNodeOutput(IrContextTree *context, NodeProgram *program) {
			// TODO: remove?
			IrReferenceInfo info;
			IrReferenceQuery query;
			query.inputContext = context;
			query.recordErrors = false;
			IrParserStructure *reference = getImmediateReference(query, &info);

			if (reference == nullptr) return nullptr;
			else return reference->generateNodeOutput(info.newContext, program);
		}
	};

} /* namespace piranha */

#endif /* PIRANHA_ATTRIBUTE_H */
