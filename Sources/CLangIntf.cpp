/*	$Id$
	
	Copyright 1996, 1997, 1998, 2002
	        Hekkelman Programmatuur B.V.  All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	1. Redistributions of source code must retain the above copyright notice,
	   this list of conditions and the following disclaimer.
	2. Redistributions in binary form must reproduce the above copyright notice,
	   this list of conditions and the following disclaimer in the documentation
	   and/or other materials provided with the distribution.
	3. All advertising materials mentioning features or use of this software
	   must display the following acknowledgement:
	   
	    This product includes software developed by Hekkelman Programmatuur B.V.
	
	4. The name of Hekkelman Programmatuur B.V. may not be used to endorse or
	   promote products derived from this software without specific prior
	   written permission.
	
	THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
	FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
	AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
	EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
	PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
	OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
	WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
	OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
	ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 	
	
	Created: 09/19/97 10:49:36
*/

#include "pe.h"
#include "PText.h"
#include "CLangIntf.h"
#include "CKeywords.h"
#include "PApp.h"
#include "CLanguageAddOn.h"
#include "CAlloca.h"
#include "Utils.h"
#include "HError.h"
#include "HAppResFile.h"
#include "HPreferences.h"
#include "HColorUtils.h"
#include <algorithm>

unsigned char *CLangIntf::sfWordBreakTable = NULL;

class ext {
public:
	ext();
	ext(const char *e);
	
	bool operator<(const ext& e) const;
	bool operator==(const ext& e) const;
	
	char t[12];
};
		
static map<ext, CLangIntf*> sInterfaces;
static CLangIntf *sDefault;
vector<CLangIntf*> CLangIntf::fInterfaces;

ext::ext()
{
	t[0] = 0;
} /* CLangIntf::ext::ext */

ext::ext(const char *e)
{
	if (strlen(e) > 11) THROW(("Extension `%s' is too long", e));
	
	strcpy(t, e);
} /* CLangIntf::ext::ext */

bool ext::operator<(const ext& e) const
{
	return strcmp(t, e.t) < 0;
} /* CLangIntf::ext::operator< */

bool ext::operator==(const ext& e) const
{
	return strcmp(t, e.t) == 0;
} /* CLangIntf::ext::operator== */

#pragma mark -

CLangIntf::CLangIntf()
{
	if (sfWordBreakTable == NULL)
	{
		sfWordBreakTable = (unsigned char *)HResources::GetResource('WBrT', 128);
		if (sfWordBreakTable == NULL) THROW(("Missing Resource!"));
	}
	
//	fImage = -1;

	fBalance = NULL;
	fScanForFunctions = NULL;
	fColorLine = NULL;
	fFindNextWord = NULL;
	fLanguage = "None";
	fExtensions = "";
	fKeywordFile = NULL;
	fLineCommentStart = fLineCommentEnd = "";
} /* CLangIntf::CLangIntf */

CLangIntf::CLangIntf(const char *path, image_id image)
{
	if (sfWordBreakTable == NULL)
	{
		sfWordBreakTable = (unsigned char *)HResources::GetResource('WBrT', 128);
		if (sfWordBreakTable == NULL) THROW(("Missing Resource!"));
	}
	
	fImage = image;
	if (fImage < 0) THROW(("Error loading language extension: %s", strerror(fImage)));

	if (get_image_symbol(fImage, "Balance", B_SYMBOL_TYPE_TEXT, (void**)&fBalance) != B_OK)
		fBalance = NULL;
	if (get_image_symbol(fImage, "ScanForFunctions", B_SYMBOL_TYPE_TEXT, (void**)&fScanForFunctions) != B_OK)
		fScanForFunctions = NULL;
	if (get_image_symbol(fImage, "ColorLine", B_SYMBOL_TYPE_TEXT, (void**)&fColorLine) != B_OK)
		fColorLine = NULL;
	if (get_image_symbol(fImage, "FindNextWord", B_SYMBOL_TYPE_TEXT, (void**)&fFindNextWord) != B_OK)
		fFindNextWord = NULL;
	FailOSErr(get_image_symbol(fImage, "kLanguageName", B_SYMBOL_TYPE_DATA, (void**)&fLanguage));
	FailOSErr(get_image_symbol(fImage, "kLanguageExtensions", B_SYMBOL_TYPE_DATA, (void**)&fExtensions));
	FailOSErr(get_image_symbol(fImage, "kLanguageCommentStart", B_SYMBOL_TYPE_DATA, (void**)&fLineCommentStart));
	FailOSErr(get_image_symbol(fImage, "kLanguageCommentEnd", B_SYMBOL_TYPE_DATA, (void**)&fLineCommentEnd));
	FailOSErr(get_image_symbol(fImage, "kLanguageKeywordFile", B_SYMBOL_TYPE_DATA, (void**)&fKeywordFile));

	if (strlen(fKeywordFile))
		GenerateKWTables(fKeywordFile, path, ec, accept, base, nxt, chk);
} /* CLangIntf::CLangIntf */

CLangIntf::~CLangIntf()
{
	delete accept;
	delete base;
	delete nxt;
	delete chk;
} /* CLangIntf::~CLangIntf */

template <class T>
void AddInterface(char *s, T* i)
{
	char *e = strtok(s, ";");
	
	while (e)
	{
		sInterfaces[e] = i;
		e = strtok(NULL, ";");
	}
	
	free(s);
} /* AddInterface */

void CLangIntf::SetupLanguageInterfaces()
{
	sDefault = new CLangIntf();
	AddInterface(strdup(""), sDefault);

	char path[PATH_MAX];
	
	BPath p;
	BEntry e;
	gAppDir.GetEntry(&e);
	e.GetPath(&p);
	strcpy(path, p.Path());

	strcat(path, "/Languages/");
	
	char plug[PATH_MAX];
	DIR *dir = opendir(path);

	if (!dir)
		return;
	
	struct dirent *dent;
	struct stat stbuf;

	while ((dent = readdir(dir)) != NULL)
	{
		strcpy(plug, path);
		strcat(plug, dent->d_name);
		status_t err = stat(plug, &stbuf);
		if (!err && S_ISREG(stbuf.st_mode) &&
			strcmp(dent->d_name, ".") && strcmp(dent->d_name, ".."))
		{
			image_id next;
			char *l;

			next = load_add_on(plug);
			if (next > B_ERROR &&
				(err = get_image_symbol(next, "kLanguageName", B_SYMBOL_TYPE_DATA, (void**)&l)) == B_OK)
			{
				if (strlen(l) > 28) THROW(("Language name too long"));

				CLangIntf *intf = new CLangIntf(plug, next);
				fInterfaces.push_back(intf);
				
				const char *s = intf->Extensions();
				AddInterface(strdup(s), intf);
			}
		}
	}

	ChooseDefault();
} /* CLangIntf::SetupLanguageInterfaces */

CLangIntf* CLangIntf::FindIntf(const char *filename)
{
	char *e;
	
	if (filename)
	{
		try
		{
			if ((e = strrchr(filename, '.')) != NULL && sInterfaces.count(e + 1))
				return sInterfaces[e + 1];
			
			if (strlen(filename) < 11 && sInterfaces.count(filename))
				return sInterfaces[filename];
		}
		catch (...) {}
	}

	return sDefault;
} /* CLangIntf::FindIntf */

static const char *skip(const char *txt)
{
	while (*txt)
	{
		switch (*txt)
		{
			case '\'':
				while (*++txt)
				{
					if (*txt == '\'')
						break;
					if (*txt == '\\' && txt[1])
						txt++;
				}
				break;
			
			case '"':
				while (*++txt)
				{
					if (*txt == '"')
						break;
					if (*txt == '\\' && txt[1])
						txt++;
				}
				break;
				
			case '/':
				if (txt[1] == '*')
				{
					txt += 2;
					while (*txt && ! (*txt == '*' && txt[1] == '/'))
						txt++;
				}
				else if (txt[1] == '/')
				{
					txt += 2;
					while (*txt && *txt != '\n')
						txt++;
				}
				break;
			
			case '{':
			case '[':
			case '(':
			case ')':
			case ']':
			case '}':
				return txt;
		}
		txt++;
	}
	
	return txt;
} // skip

static bool InternalBalance(CLanguageProxy& proxy, int& start, int& end)
{
	const char *txt = proxy.Text(), *et;
	int size = proxy.Size();
	
	if (start < 0 || start > end || end > size)
		return false;
	
	et = txt + end;
	
	stack<int> bls, sbls, pls;
	
	while (*txt && txt < et)
	{
		switch (*txt)
		{
			case '{':	bls.push(txt - proxy.Text());	break;
			case '[':	sbls.push(txt - proxy.Text());	break;
			case '(':	pls.push(txt - proxy.Text());	break;
			case '}':	if (!bls.empty()) bls.pop();		break;
			case ']':	if (!sbls.empty()) sbls.pop();		break;
			case ')':	if (!pls.empty()) pls.pop();		break;
		}
		txt = skip(txt + 1);
	}
	
	char ec = 0, oc = 0;
	stack<int> *s = NULL;
	
	int db, dsb, dp;
	
	db = bls.empty() ? -1 : start - bls.top();
	dsb = sbls.empty() ? -1 : start - sbls.top();
	dp = pls.empty() ? -1 : start - pls.top();
	
	if (db < 0 && dsb < 0 && dp < 0)
		return false;
	
	if (db >= 0 && (dsb < 0 || db < dsb) && (dp < 0 || db < dp))
	{
		oc = '{';
		ec = '}';
		s = &bls;
	}
	
	if (dsb >= 0 && (db < 0 || dsb < db) && (dp < 0 || dsb < dp))
	{
		oc= '[';
		ec = ']';
		s = &sbls;
	}
	
	if (dp >= 0 && (dsb < 0 || dp < dsb) && (db < 0 || dp < db))
	{
		oc = '(';
		ec = ')';
		s = &pls;
	}
	
	if (ec)
	{
		int l = 1;
		
		while (*txt)
		{
			if (*txt == ec)
			{
				if (--l == 0)
				{
					start = s->top() + 1;
					end = txt - proxy.Text();
					return true;
				}
				if (!s->empty()) s->pop();
			}
			else if (*txt == oc)
			{
				l++;
				s->push(0);
			}

			txt = skip(txt + 1);
		}
	}
	
	return false;
} /* InternalBalance */

bool CLangIntf::Balance(PText& text, int& start, int& end)
{
	try
	{
		CLanguageProxy proxy(*this, text);
		
		if (fBalance)
			return fBalance(proxy, start, end);
		else
			return InternalBalance(proxy, start, end);
	}
	catch (...)
	{
		return false;
	}
} /* CLangIntf::Balance */

void CLangIntf::Balance(PText& text)
{
	try
	{
		int start = min(text.Anchor(), text.Caret());
		int end = max(text.Anchor(), text.Caret());
		
		if (! Balance(text, start, end))
			THROW((0));

		if (start == min(text.Anchor(), text.Caret()) &&
			end == max(text.Anchor(), text.Caret()))
		{
			start--; end++;
			if (! Balance(text, start, end))
				THROW((0));
		}

		text.ChangeSelection(start, end);
	}
	catch (...)
	{
		beep();
	}
} /* CLangIntf::Balance */

void CLangIntf::ColorLine(const char *text, int size, int& state,
		int *starts, rgb_color *colors)
{
	try
	{
		if (fColorLine)
		{
			CLanguageProxy proxy(*this, text, size, 0, starts, colors);
			fColorLine(proxy, state);
		}
		else if (starts)
		{
			starts[0] = 0;
			colors[0] = gColor[kTextColor];
		}
	}
	catch (...)
	{
		beep();
	}
} /* CLangIntf::ColorLine */

void CLangIntf::ScanForFunctions(PText& text, CFunctionScanHandler& handler)
{
	try
	{
		CLanguageProxy proxy(*this, text, &handler);

		if (fScanForFunctions)
			fScanForFunctions(proxy);
	}
	catch(...)
	{
		beep();
	}
} /* CLangIntf::ScanForFunctions */

int CLangIntf::FindNextWord(PText& text, int offset, int& mlen)
{
	try
	{
		if (fFindNextWord)
		{
			int line = text.Offset2Line(offset);
			int size;
			
			if (line >= text.LineCount() - 1)
				size = min(text.Size() - offset, 1024);
			else
				size = min(text.LineStart(line + 1) - offset, 1024);
			
			CAlloca txt(size + 1);
			text.TextBuffer().Copy(txt, offset, size);
			txt[size] = 0;
			
			CLanguageProxy proxy(*this, txt, size, text.Encoding());
			int result = fFindNextWord(proxy);

			txt[result + 1] = 0;
			mlen = mstrlen(txt);
			
			return offset + result;
		}
		else
		{
			int mark = offset, i = offset;
			int unicode, state, len, iLen;
			
			state = 1;
			mlen = 0;
			iLen = 0;
			
			while (state > 0 && i < text.Size())
			{
				text.TextBuffer().CharInfo(i, unicode, len);
				
				int cl = 0;
				
				if (unicode == '\n')
					cl = 3;
				else if (isspace_uc(unicode))
					cl = 2;
				else if (isalnum_uc(unicode))
					cl = 4;
				else
					switch (unicode)
					{
						case 160:
						case 8199:
						case 8209:
							cl = 1;
							break;
						case '&':
						case '*':
						case '+':
						case '-':
						case '/':
						case '<':
						case '=':
						case '>':
						case '\\':
						case '^':
						case '|':
							cl = 5;
							break;
						default:
							cl = 4;
					}
				
				unsigned char t = sfWordBreakTable[(state - 1) * 6 + cl];

				state = t & 0x7f;

				if (t & 0x80)
				{
					mark = i + len - 1;
					mlen = iLen + 1;
				}

				iLen++;
				i += len;
			}

			return mark;
		}
	}
	catch (HErr& e)
	{
		e.DoError();
//		beep();
	}

	return offset;
} /* CLangIntf::FindNextWord */

CLangIntf* CLangIntf::NextIntf(int& cookie)
{
	if (cookie >= 0 && cookie < fInterfaces.size())
		return fInterfaces[cookie++];
	else
		return NULL;
} /* CLangIntf::NextIntf */

const char *CLangIntf::Extensions() const
{
	char extPref[64];
	
	if (strlen(fLanguage) > 32) THROW(("Language name too long: %s", fLanguage));
	strcpy(extPref, fLanguage);
	strcat(extPref, ".ext");
	
	return gPrefs->GetPrefString(extPref, fExtensions);
} /* CLangIntf::Extensions */

void CLangIntf::SetExtensions(const char *ext)
{
	char extPref[32];
	
	strcpy(extPref, fLanguage);
	strcat(extPref, ".ext");
	
	gPrefs->SetPrefString(extPref, ext);
} /* CLangIntf::SetExtensions */

void CLangIntf::ChooseDefault()
{
	const char *d = gPrefs->GetPrefString("def lang", "None");
	vector<CLangIntf*>::iterator i;
	
	for (i = fInterfaces.begin(); i != fInterfaces.end(); i++)
	{
		if (strcmp(d, (*i)->Name()) == 0)
		{
			sDefault = *i;
			return;
		}
	}
} /* CLangIntf::ChooseDefault */

int CLangIntf::GetIndex(const CLangIntf* intf)
{
	vector<CLangIntf*>::iterator i = find(fInterfaces.begin(), fInterfaces.end(), intf);
	if (i == fInterfaces.end())
		return -1;
	else
		return i - fInterfaces.begin();
} // CLangIntf::GetIndex

CLangIntf* CLangIntf::FindByName(const char *language)
{
	vector<CLangIntf*>::iterator i;
	
	for (i = fInterfaces.begin(); i != fInterfaces.end(); i++)
	{
		if (strcmp(language, (*i)->Name()) == 0)
			return *i;
	}
	
	return sDefault;
} // CLangIntf::FindByName


// #pragma mark -

CFunctionScanHandler::CFunctionScanHandler()
{
} // CFunctionScanHandler::CFunctionScanHandler

CFunctionScanHandler::~CFunctionScanHandler()
{
} // CFunctionScanHandler::~CFunctionScanHandler()

void CFunctionScanHandler::AddFunction(const char *name, const char *match,
	int offset, bool italic)
{
} // CFunctionScanHandler::AddFunction

void CFunctionScanHandler::AddInclude(const char *name, const char *open,
	bool italic)
{
} // CFunctionScanHandler::AddInclude

void CFunctionScanHandler::AddSeparator()
{
}

