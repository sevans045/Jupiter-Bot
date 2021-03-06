/**
 * Copyright (C) 2016 Jessica James.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Written by Jessica James <jessica.aj@outlook.com>
 */

#if !defined _IRC_CORE_H_HEADER
#define _IRC_CORE_H_HEADER

#include "Jupiter/Plugin.h"
#include "Jupiter/Reference_String.h"

class IRCCorePlugin : public Jupiter::Plugin
{
public:
	/**
	* @brief Initializes the plugin
	*/
	virtual bool initialize() override;

	/**
	* @brief Called when there is a rehash
	*
	* @return 0 always.
	*/
	virtual int OnRehash() override;

	/**
	* @brief Cycles through IRC servers for new data
	*/
	virtual int think() override;

	/**
	* @brief This is called when a GenericCommand is instantiated.
	*/
	virtual void OnGenericCommandAdd(Jupiter::GenericCommand &command);

	/**
	* @brief This is called when a GenericCommand is deleted.
	*/
	virtual void OnGenericCommandRemove(Jupiter::GenericCommand &command);

	/**
	* @brief Destructor for the IRCCorPlugin class
	*/
	~IRCCorePlugin();

private:
	Jupiter::ArrayList<GenericCommandWrapperIRCCommand> m_wrapped_commands;
};

#endif // _IRC_CORE_H_HEADER