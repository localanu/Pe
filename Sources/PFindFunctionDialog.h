// PFindFunctionDialog.h

#ifndef PFINDFUNCTIONDIALOG_H
#define PFINDFUNCTIONDIALOG_H

#include <Debug.h>
#include <Messenger.h>

#include "PText.h"
#include "CFilterChoiceDialog.h"
#include "CLangIntf.h"

// PFindFunctionDialogController
class PFindFunctionDialogController : private CFilterChoiceDialog::Listener,
	private CFunctionScanHandler {
public:
	PFindFunctionDialogController(PText *text);
	~PFindFunctionDialogController();

private:
	virtual void FilterChoiceDialogCommitted(CFilterChoiceDialog *dialog,
		CFilterChoiceItem *choice);
	virtual void FilterChoiceDialogAborted(CFilterChoiceDialog *dialog);

	virtual	void AddFunction(const char *name, const char *match, int offset,
		bool italic);
	virtual	void AddInclude(const char *name, const char *open, bool italic);
	virtual	void AddSeparator();

private:
	struct Choice;
	class ChoiceModel;

	CFilterChoiceDialog	*fDialog;
	BMessenger			fTarget;
	ChoiceModel			*fChoiceModel;
};

#endif	// PFINDFUNCTIONDIALOG_H
