/* ************************************************************************
 * Copyright (c) 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ************************************************************************ */

#ifndef AOCLSPARSE_ITSOL_OPTIONS_HPP_
#define AOCLSPARSE_ITSOL_OPTIONS_HPP_

#include "aoclsparse.h"
#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

/*
 * Options Registry
 * ================
 *
 * aoclsparse_options namespace provides a Registry class that "registers"
 * options and an Option class that defines an "option" element.
 * Options can be of any four classes: Integer, Real (float or double,
 * templated), Boolean or String. The registry class
 * defines a method Called "Register" to add a new option.
 * Registered options can be set using Registry.SetOption,
 * queried using Registry.GetOption and for strings Registry.GetKey, and
 * prety-printed using either Registry.PrintOptions or Registry.PrintDetails.
 *
 * Options have a "name" that is used to distiguish them, so this should be
 * unique among the ALL the registered options (of any Option class).
 * Furthermore, the "name" string is sanitized before using, that is, it is
 * trimmed and blanks squeezed. Do not register the same option twice.
 * No checks on option registry are done.
 *
 * Usage:
 * See aoclsparse_list_options.hpp for details on adding new options and
 * aoclsparse_itsol_functions.hpp on how to query.
 *
 * Quering options have a certain cost and should be done only once at the
 * beggining of the solver or when initializing the internal data.
 *
 *
 * Option{Int|Real|Bool|String} Constructors
 * throw invalid_argument
 *
 * Option{Int|Real|Bool|String} Setters
 * throw invalid_argument, and
 *       out_of_range
 *
 * Register Setters returns an integer
 * iflag 0 - success
 *       1 - out of range
 *       2 - invalid argument
 *       3 - option not found
 *       4 - Registry is locked. Option cannot be set
 */

namespace aoclsparse_options
{
    using namespace std;

    typedef enum lbound_type
    {
        m_inf = 0,
        greaterthan,
        greaterequal
    } lbound_t;
    typedef enum ubound_type
    {
        p_inf = 0,
        lessthan,
        lessequal
    } ubound_t;

    // This class provides common utility methods
    // used within Option???? and Registry classes
    class OptionUtility
    {
    public:
        // Method to trim and squeeze [:blank:]s, and convert to lower case
        void PrepareString(string& str)
        {
            regex ltrim("^[[:space:]]+");
            regex rtrim("[[:space:]]+$");
            regex squeeze("[[:space:]]+");

            str = regex_replace(str, ltrim, string(""));
            str = regex_replace(str, rtrim, string(""));
            str = regex_replace(str, squeeze, string(" "));
            transform(str.begin(), str.end(), str.begin(), ::tolower);
        };
    };

    class OptionBase
    {
        OptionUtility util;

    protected:
        string         name; // name i.e. "Iteration Limit"
        aoclsparse_int id; // unique id (not used for now)
        string         desc; // brief description (free text)
        aoclsparse_int setby; // 0 default, 1 user, 2 solver
        bool           hidden; // option is hidden to the user (not used)
        aoclsparse_int pgrp; // printing group (pretty print options, not used)

        OptionBase(){};
        ~OptionBase(){};

        const string setby_l[3] = {"(default)", "(user)", "(solver)"};

        virtual string PrintOption(void)  = 0;
        virtual string PrintDetails(void) = 0;

        void SetName(string str)
        {
            name = str;
            util.PrepareString(name);
        }

    public:
        string GetName(void)
        {
            return name;
        };
    };

    class OptionInt : public OptionBase
    {
        // default value for option
        aoclsparse_int vdefault = 0;
        // actual value of the option
        aoclsparse_int value;
        // lower bound value for option
        aoclsparse_int lower;
        // lower bound type (none (-inf), greater than..., greater or equal than...)
        lbound_t lbound;
        // upper value for option
        aoclsparse_int upper;
        // upper bound type (none (+inf), less than..., less or equal than...)
        ubound_t ubound;

    public:
        OptionInt(const string         name,
                  const aoclsparse_int id,
                  const string         desc,
                  const bool           hidden,
                  const aoclsparse_int pgrp,
                  const aoclsparse_int lower,
                  const lbound_t       lbound,
                  const aoclsparse_int upper,
                  const ubound_t       ubound,
                  const aoclsparse_int vdefault)
        {
            if(upper != upper || lower != lower)
                throw invalid_argument("Either lower or upper are not finite.");
            if(upper < lower)
                throw invalid_argument("Invalid bounds for option value: lower > upper.");
            // Check bounds (special case)
            // l = u and l <  value <= u OR
            //           l <  value <  u OR
            //           l <= value <  u
            if(lower == upper && lbound != m_inf && ubound != p_inf)
            {
                if(!(lbound == greaterequal && ubound == lessequal))
                    throw invalid_argument("Invalid bounds for option.");
            }
            if(vdefault != vdefault)
            {
                throw invalid_argument("Invalid default value.");
            }
            else
            {
                if(CheckRange(vdefault, lower, lbound, upper, ubound) != 0)
                    throw invalid_argument("Default value out-of-bounds");
            }
            SetName(name);
            if(OptionInt::name == "")
                throw invalid_argument("Invalid name (string reduced to zero-length).");
            OptionInt::id       = id;
            OptionInt::desc     = desc;
            OptionInt::hidden   = hidden;
            OptionInt::pgrp     = pgrp;
            OptionInt::vdefault = vdefault;
            OptionInt::value    = vdefault;
            OptionInt::setby    = 0;
            OptionInt::lower    = lower;
            OptionInt::lbound   = lbound;
            OptionInt::upper    = upper;
            OptionInt::ubound   = ubound;
        };
        ~OptionInt(){};

        string PrintOption(void)
        {
            ostringstream rec;
            rec << " " << name << " = " << value << endl;
            return rec.str();
        }

        string PrintDetails(void)
        {
            // string        str;
            ostringstream rec;
            rec << "Begin Option [Integer]" << endl;
            rec << "   Name: '" << name << "'" << endl;
            rec << "   Value: " << value << "     [default: " << vdefault << "]" << endl;
            rec << "   Range: ";
            if(lbound == m_inf && ubound == p_inf)
            {
                rec << "unbounded" << endl;
            }
            else
            {
                if(lbound == greaterequal)
                {
                    rec << lower << " <= ";
                }
                else if(lbound == greaterthan)
                {
                    rec << lower << " < ";
                }
                rec << "value ";
                if(ubound == lessequal)
                {
                    rec << " <= " << upper;
                }
                else if(ubound == lessthan)
                {
                    rec << " < " << upper;
                }
                rec << endl;
            }
            rec << "   Id: " << id << endl;
            rec << "   Desc: " << desc << endl;
            rec << "   Hidden: " << boolalpha << hidden << endl;
            rec << "   Set-by: " << setby_l[setby] << endl;
            rec << "   Print-group: " << pgrp << endl;

            rec << "End Option" << endl;

            return rec.str();
        }

        aoclsparse_int CheckRange(const aoclsparse_int value,
                                  const aoclsparse_int lower,
                                  const lbound_t       lbound,
                                  const aoclsparse_int upper,
                                  const ubound_t       ubound)
        {
            aoclsparse_int iflag = 0;
            // check that it is within range (lower bound)
            if((lbound == greaterthan) && (value <= lower))
                iflag += 1;
            else if((lbound == greaterequal) && (value < lower))
                iflag += 2;

            // check that it is within range (upper bound)
            if((ubound == lessthan) && (value >= upper))
                iflag += 10;
            else if((ubound == lessequal) && (value > upper))
                iflag += 20;

            return iflag;
        }

        aoclsparse_int GetIntegerValue(void) const
        {
            return value;
        };
        void SetInvegerValue(aoclsparse_int value, aoclsparse_int setby = 0)
        {
            aoclsparse_int iflag = 0;

            if(value != value)
            {
                throw invalid_argument("Passed option value is invalid.");
            }

            if(CheckRange(value, lower, lbound, upper, ubound) != 0)
            {
                throw out_of_range("Value out-of-bounds.");
            }

            OptionInt::value = value;
            OptionInt::setby = setby ? setby : 0;
        };
    };

    template <typename T>
    class OptionReal : public OptionBase
    {
        // default value for option
        T vdefault = 0.0;
        // actual value of the option
        T value;
        // lower bound value for option
        T lower;
        // lower bound type (none (-inf), greater than..., greater or equal than...)
        lbound_t lbound;
        // upper value for option
        T upper;
        // upper bound type (none (+inf), less than..., less or equal than...)
        ubound_t ubound;

    public:
        OptionReal(const string         name,
                   const aoclsparse_int id,
                   const string         desc,
                   const bool           hidden,
                   const aoclsparse_int pgrp,
                   const T              lower,
                   const lbound_t       lbound,
                   const T              upper,
                   const ubound_t       ubound,
                   const T              vdefault)
        {
            if(upper != upper || lower != lower)
                throw invalid_argument("Either lower or upper are not finite.");
            if(upper < lower)
                throw invalid_argument("Invalid bounds for option value: lower > upper.");
            // Check bounds
            // l = u and l <  value <= u OR
            //           l <= value <  u
            if(lower == upper && lbound != m_inf && ubound != p_inf)
            {
                if(!(lbound == greaterequal && ubound == lessequal))
                    throw invalid_argument("Invalid bounds for option.");
            }
            if(vdefault != vdefault)
            {
                throw invalid_argument("Invalid default value.");
            }
            else
            {
                if(CheckRange(vdefault, lower, lbound, upper, ubound) != 0)
                    throw invalid_argument("Default value out-of-bounds.");
            }
            SetName(name);
            if(OptionReal::name == "")
                throw invalid_argument("Invalid name (string reduced to zero-length).");
            OptionReal::id       = id;
            OptionReal::desc     = desc;
            OptionReal::hidden   = hidden;
            OptionReal::pgrp     = pgrp;
            OptionReal::vdefault = vdefault;
            OptionReal::value    = vdefault;
            OptionReal::setby    = 0;
            OptionReal::lower    = lower;
            OptionReal::lbound   = lbound;
            OptionReal::upper    = upper;
            OptionReal::ubound   = ubound;
        };

        ~OptionReal(){};

        string PrintOption(void)
        {
            ostringstream rec;
            rec << " " << name << " = " << value << endl;
            return rec.str();
        }

        string PrintDetails(void)
        {
            // string        str;
            ostringstream rec;
            rec << "Begin Option [Real]" << endl;
            rec << "   Name: '" << name << "'" << endl;
            rec << "   Value: " << value << "     [default: " << vdefault << "]" << endl;
            rec << "   Range: ";
            if(lbound == m_inf && ubound == p_inf)
            {
                rec << "unbounded" << endl;
            }
            else
            {
                if(lbound == greaterequal)
                {
                    rec << lower << " <= ";
                }
                else if(lbound == greaterthan)
                {
                    rec << lower << " < ";
                }
                rec << "value ";
                if(ubound == lessequal)
                {
                    rec << " <= " << upper;
                }
                else if(ubound == lessthan)
                {
                    rec << " < " << upper;
                }
                rec << endl;
            }
            rec << "   Id: " << id << endl;
            rec << "   Desc: " << desc << endl;
            rec << "   Hidden: " << boolalpha << hidden << endl;
            rec << "   Set-by: " << setby_l[setby] << endl;
            rec << "   Print-group: " << pgrp << endl;

            rec << "End Option" << endl;

            return rec.str();
        }

        aoclsparse_int
            CheckRange(T value, T lower, const lbound_t lbound, T upper, const ubound_t ubound)
        {
            aoclsparse_int iflag = 0;
            // check that it is within range (lower bound)
            if((lbound == greaterthan) && (value <= lower))
                iflag += 1;
            else if((lbound == greaterequal) && (value < lower))
                iflag += 2;

            // check that it is within range (upper bound)
            if((ubound == lessthan) && (value >= upper))
                iflag += 10;
            else if((ubound == lessequal) && (value > upper))
                iflag += 20;

            return iflag;
        }

        T GetRealValue(void) const
        {
            return value;
        };
        void SetRealValue(T value, aoclsparse_int setby = 0)
        {
            aoclsparse_int iflag = 0;

            if(value != value)
            {
                throw invalid_argument("Passed option value is invalid.");
            }

            if(CheckRange(value, lower, lbound, upper, ubound) != 0)
            {
                string msg = "Value out-of-bounds.";
                throw out_of_range(msg);
            }

            OptionReal::value = value;
            OptionReal::setby = setby ? setby : 0;
        };
    };

    class OptionBool : public OptionBase
    {
        // default value for option
        bool vdefault;
        // actual value of the option
        bool value;

    public:
        OptionBool(const string         name,
                   const aoclsparse_int id,
                   const string         desc,
                   const bool           hidden,
                   const aoclsparse_int pgrp,
                   const bool           vdefault)
        {
            if(vdefault != vdefault)
                throw invalid_argument("Default value is not finite.");
            SetName(name);
            if(OptionBool::name == "")
                throw invalid_argument("Invalid name (string reduced to zero-length).");
            OptionBool::id       = id;
            OptionBool::desc     = desc;
            OptionBool::hidden   = hidden;
            OptionBool::pgrp     = pgrp;
            OptionBool::vdefault = vdefault;
            OptionBool::value    = vdefault;
            OptionBool::setby    = 0;
        };
        ~OptionBool(){};

        string PrintOption(void)
        {
            ostringstream rec;
            rec << " " << name << " = " << value << endl;
            return rec.str();
        }

        string PrintDetails(void)
        {
            ostringstream rec;
            rec << "Begin Option [Boolean]" << endl;
            rec << "   Name: '" << name << "'" << endl;
            rec << "   Value: " << value << "     [default: " << vdefault << "]" << endl;
            rec << "   Id: " << id << endl;
            rec << "   Desc: " << desc << endl;
            rec << "   Hidden: " << boolalpha << hidden << endl;
            rec << "   Set-by: " << setby_l[setby] << endl;
            rec << "   Print-group: " << pgrp << endl;
            rec << "End Option" << endl;
            return rec.str();
        }

        bool GetBoolValue(void) const
        {
            return value;
        };
        void SetBoolValue(bool value, aoclsparse_int setby = 0)
        {
            if(value != value)
            {
                throw invalid_argument("passed option value is invalid.");
            }
            OptionBool::value = value;
            OptionBool::setby = setby ? setby : 0;
        };
    };

    // add OptionString class
    class OptionString : public OptionBase
    {
        // default label
        string vdefault;
        // selected label
        string                      value;
        map<string, aoclsparse_int> labels;

    public:
        OptionString(const string                      name,
                     const aoclsparse_int              id,
                     const string                      desc,
                     const bool                        hidden,
                     const aoclsparse_int              pgrp,
                     const map<string, aoclsparse_int> labels,
                     const string                      vdefault)
        {
            OptionUtility u;
            bool          keyok  = false;
            OptionString::id     = id;
            OptionString::desc   = desc;
            OptionString::hidden = hidden;
            OptionString::pgrp   = pgrp;
            string label;

            if(labels.size() == 0)
            {
                throw invalid_argument("Label's map must contain at least one entry.");
            }
            SetName(name);
            if(OptionString::name == "")
                throw invalid_argument("Invalid name (string reduced to zero-length).");

            for(const auto& entry : labels)
            {
                label = entry.first;
                u.PrepareString(label);
                if(label == "")
                    throw invalid_argument("Invalid option value (string reduced to zero-length).");
                auto const ok = OptionString::labels.insert({label, entry.second});
                if(not ok.second)
                {
                    throw invalid_argument("Failed to insert a label, duplicate?");
                }
            }
            // check that default is valid
            label = vdefault;
            u.PrepareString(label);

            auto it = OptionString::labels.find(label);
            if(it == OptionString::labels.end())
            {
                throw invalid_argument("Default label is invalid.");
            }
            OptionString::vdefault = label;
            OptionString::value    = label;
            OptionString::setby    = 0;
        };
        ~OptionString(){};

        string PrintOption(void)
        {
            ostringstream rec;
            rec << " " << name << " = " << value << endl;
            return rec.str();
        }
        string PrintDetails(void)
        {
            ostringstream rec;
            rec << "Begin Option [String]" << endl;
            rec << "   Name: '" << name << "'" << endl;
            rec << "   Value: '" << value << "'     [default: '" << vdefault << "']" << endl;
            rec << "   Valid values: " << endl;
            for(auto const it : labels)
            {
                rec << "      '" << it.first << "' : " << it.second << endl;
            }
            rec << "   Id: " << id << endl;
            rec << "   Desc: " << desc << endl;
            rec << "   Hidden: " << boolalpha << hidden << endl;
            rec << "   Set-by: " << setby_l[setby] << endl;
            rec << "   Print-group: " << pgrp << endl;
            rec << "End Option" << endl;
            return rec.str();
        }

        string GetStringValue(void) const
        {
            return value;
        };
        string GetStringValue(aoclsparse_int& key) const
        {
            key = labels.at(value);
            return value;
        }
        void SetStringValue(const string value, const aoclsparse_int setby = 0)
        {
            OptionUtility  u;
            aoclsparse_int iflag = 1;
            string         val(value);
            u.PrepareString(val);

            // check that value is a valid key
            auto pos = labels.find(val);
            if(pos == labels.end())
            {
                throw std::invalid_argument("Unrecognized value for this option.");
            }

            OptionString::value = val;
            OptionString::setby = setby ? setby : 0;
        };
    };

    // Option registry
    template <typename T>
    class OptionRegistry
    {
        OptionUtility util;
        bool          readonly = false;
        // These can change under the hood for std::map <const string, Option???>
        vector<OptionInt>     IntRegistry;
        vector<OptionReal<T>> RealRegistry;
        vector<OptionBool>    BoolRegistry;
        vector<OptionString>  StringRegistry;

    public:
        OptionRegistry()
        {
            readonly = false;
        };
        ~OptionRegistry(){};
        void Lock(void)
        {
            readonly = true;
        }
        void UnLock(void)
        {
            readonly = false;
        }
        // Registers
        aoclsparse_int Register(const OptionInt i)
        {
            // if (already_registered)
            //    return 2;
            IntRegistry.push_back(i);
            return 0;
        };
        aoclsparse_int Register(const OptionReal<T> r)
        {
            // if (already_registered)
            //    return 2;
            RealRegistry.push_back(r);
            return 0;
        };
        aoclsparse_int Register(const OptionBool b)
        {
            // if (already_registered)
            //    return 2;
            BoolRegistry.push_back(b);
            return 0;
        };
        aoclsparse_int Register(const OptionString s)
        {
            // if (already_registered)
            //    return 2;
            StringRegistry.push_back(s);
            return 0;
        };
        // Registry Setter
        // name - option name
        // value - value to set
        // setby - flag 0 (default), 1 (user), 2 (solver)
        aoclsparse_int
            SetOption(const string name, const aoclsparse_int value, const aoclsparse_int setby)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            vector<OptionInt>::iterator it
                = find_if(IntRegistry.begin(), IntRegistry.end(), [pname](OptionInt o) {
                      return o.GetName() == pname;
                  });
            if(it != IntRegistry.end())
            {
                if(readonly)
                    return 4;
                try
                {
                    (*it).SetInvegerValue(value, setby);
                }
                catch(const out_of_range& oor)
                {
                    return 1;
                }
                catch(const invalid_argument& iarg)
                {
                    return 2;
                }
            }
            else
            {
                return 3;
            }
            return 0;
        };
        aoclsparse_int SetOption(const string name, const T value, const aoclsparse_int setby)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            typename vector<OptionReal<T>>::iterator it
                = find_if(RealRegistry.begin(), RealRegistry.end(), [pname](OptionReal<T> o) {
                      return o.GetName() == pname;
                  });
            if(it != RealRegistry.end())
            {
                if(readonly)
                    return 4;
                try
                {
                    (*it).SetRealValue(value, setby);
                }
                catch(const out_of_range& oor)
                {
                    return 1;
                }
                catch(const invalid_argument& iarg)
                {
                    return 2;
                }
            }
            else
            {
                return 3; // option not found
            }
            return 0;
        };
        aoclsparse_int SetOption(const string name, const bool value, const aoclsparse_int setby)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            vector<OptionBool>::iterator it
                = find_if(BoolRegistry.begin(), BoolRegistry.end(), [pname](OptionBool o) {
                      return o.GetName() == pname;
                  });
            if(it != BoolRegistry.end())
            {
                if(readonly)
                    return 4;
                try
                {
                    (*it).SetBoolValue(value, setby);
                }
                catch(const invalid_argument& iarg)
                {
                    return 2;
                }
            }
            else
            {
                return 3;
            }
            return 0;
        };
        aoclsparse_int SetOption(const string name, const string value, const aoclsparse_int setby)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            vector<OptionString>::iterator it
                = find_if(StringRegistry.begin(), StringRegistry.end(), [pname](OptionString o) {
                      return o.GetName() == pname;
                  });
            if(it != StringRegistry.end())
            {
                if(readonly)
                    return 4;
                try
                {
                    (*it).SetStringValue(value, setby);
                }
                catch(const out_of_range& oor)
                {
                    return 1;
                }
                catch(const invalid_argument& iarg)
                {
                    return 2;
                }
            }
            else
            {
                return 3;
            }
            return 0;
        };
        // Getters
        aoclsparse_int GetOption(const string name, aoclsparse_int& value)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            vector<OptionInt>::iterator it
                = find_if(IntRegistry.begin(), IntRegistry.end(), [pname](OptionInt o) {
                      return o.GetName() == pname;
                  });
            if(it != IntRegistry.end())
            {
                value = (*it).GetIntegerValue();
                return 0;
            }

            return 3;
        };

        aoclsparse_int GetOption(const string name, T& value)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            typename vector<OptionReal<T>>::iterator it
                = find_if(RealRegistry.begin(), RealRegistry.end(), [pname](OptionReal<T> o) {
                      return o.GetName() == pname;
                  });
            if(it != RealRegistry.end())
            {
                value = (*it).GetRealValue();
                return 0;
            }
            return 3;
        };
        aoclsparse_int GetOption(const string name, bool& value)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            vector<OptionBool>::iterator it
                = find_if(BoolRegistry.begin(), BoolRegistry.end(), [pname](OptionBool o) {
                      return o.GetName() == pname;
                  });
            if(it != BoolRegistry.end())
            {
                value = (*it).GetBoolValue();
                return 0;
            }
            return 3;
        };
        aoclsparse_int GetOption(const string name, string& value, aoclsparse_int& key)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            vector<OptionString>::iterator it
                = find_if(StringRegistry.begin(), StringRegistry.end(), [pname](OptionString o) {
                      return o.GetName() == pname;
                  });
            if(it != StringRegistry.end())
            {
                value = (*it).GetStringValue(key);
                return 0;
            }
            return 3;
        };
        aoclsparse_int GetKey(const string name, aoclsparse_int& key)
        {
            OptionUtility u;
            string        pname(name);
            u.PrepareString(pname);
            vector<OptionString>::iterator it
                = find_if(StringRegistry.begin(), StringRegistry.end(), [pname](OptionString o) {
                      return o.GetName() == pname;
                  });
            if(it != StringRegistry.end())
            {
                (*it).GetStringValue(key);
                return 0;
            }
            return 3;
        };
        // Auxiliary
        void PrintOptions(void)
        {
            // void PrintOptions(const vector<aoclsparse_int> pgrp){
            cout << "Begin Options" << endl;
            // cout << "Integer Options"
            for(auto it : IntRegistry)
                cout << it.PrintOption();
            // cout << "Real Options"
            for(auto it : RealRegistry)
                cout << it.PrintOption();
            // cout << "Boolean Options"
            for(auto it : BoolRegistry)
                cout << it.PrintOption();
            // cout << "Boolean Options"
            for(auto it : StringRegistry)
                cout << it.PrintOption();
            cout << "End Options" << endl;
        };

        void PrintDetails(void)
        {
            // void PrintOptions(const vector<aoclsparse_int> pgrp){
            cout << "Begin (detailed print of registered options)" << endl;
            for(auto it : IntRegistry)
                cout << it.PrintDetails();
            for(auto it : RealRegistry)
                cout << it.PrintDetails();
            for(auto it : BoolRegistry)
                cout << it.PrintDetails();
            for(auto it : StringRegistry)
                cout << it.PrintDetails();
            cout << "End" << endl;
        };
    };
}; // namespace aoclsparse_options
#endif
