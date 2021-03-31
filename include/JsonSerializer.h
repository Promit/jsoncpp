/*
 * Copyright (c) 2011-2012 Promit Roy
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
 */

#ifndef JSONSERIALIZER_H
#define JSONSERIALIZER_H

#include <json/json.h>
#include <type_traits>
#include <string>

enum class SerializerMode
{
	Reader,
	Writer
};

class JsonSerializer
{
private:
	//SFINAE garbage to detect whether a type has a Serialize member
	typedef char SerializeNotFound;
	struct SerializeFound { char x[2]; };
	struct SerializeFoundStatic { char x[3]; };
	
	template<typename T, void (T::*)(JsonSerializer&)>
	struct SerializeTester { };
	template<typename T, void(*)(JsonSerializer&)>
	struct SerializeTesterStatic { };
	template<typename T>
	static SerializeFound SerializeTest(SerializeTester<T, &T::Serialize>*);
	template<typename T>
	static SerializeFoundStatic SerializeTest(SerializeTesterStatic<T, &T::Serialize>*);
	template<typename T>
	static SerializeNotFound SerializeTest(...);
	
	template<typename T>
	struct HasSerialize
	{
		static const bool value = sizeof(SerializeTest<T>(0)) == sizeof(SerializeFound);
	};
	
	//Serialize using a free function defined for the type (default fallback)
	template<typename TValue>
	void SerializeImpl(TValue& value,
					   typename std::enable_if<!HasSerialize<TValue>::value >::type* dummy = 0)
	{
		//prototype for the serialize free function, so we will get a link error if it's missing
		//this way we don't need a header with all the serialize functions for misc types (eg math)
		void Serialize(TValue&, JsonSerializer&);
		
		Serialize(value, *this);
	}

	//Serialize using a member function Serialize(JsonSerializer&)
	template<typename TValue>
	void SerializeImpl(TValue& value, typename std::enable_if<HasSerialize<TValue>::value >::type* dummy = 0)
	{
		value.Serialize(*this);
	}
	
public:
	JsonSerializer(bool isWriter)
	: IsWriter(isWriter)
	{ }
	JsonSerializer(bool isWriter, Json::Value value)
		: IsWriter(isWriter), JsonValue(value)
	{ }
	JsonSerializer(SerializerMode mode)
		: IsWriter(mode == SerializerMode::Writer)
	{ }

	template<typename TKey, typename TValue>
	void Serialize(TKey key, TValue& value, typename std::enable_if<std::is_class<TValue>::value >::type* dummy = 0)
	{
		JsonSerializer subVal(IsWriter);
		if(!IsWriter)
		{
			subVal.JsonValue = JsonValue[key];
			if(subVal.JsonValue.isNull())
				return;
		}
		
		subVal.SerializeImpl(value);
		
		if(IsWriter)
			JsonValue[key] = subVal.JsonValue;
	}
		
	//Serialize a string value
	template<typename TKey>
	void Serialize(TKey key, std::string& value)
	{
		if(IsWriter)
			Write(key, value);
		else
			Read(key, value);
	}
	
	//Serialize a non class type directly using JsonCpp
	template<typename TKey, typename TValue>
	void Serialize(TKey key, TValue& value, typename std::enable_if<std::is_fundamental<TValue>::value >::type* dummy = 0)
	{
		if(IsWriter)
			Write(key, value);
		else
			Read(key, value);
	}
	
	//Serialize an enum type to JsonCpp 
	template<typename TKey, typename TEnum>
	void Serialize(TKey key, TEnum& value, typename std::enable_if<std::is_enum<TEnum>::value >::type* dummy = 0)
	{
		int ival = (int) value;
		if(IsWriter)
		{
			Write(key, ival);
		}
		else
		{
			Read(key, ival);
			value = (TEnum) ival;
		}
	}
	
	//Serialize only when writing (saving), useful for r-values
	template<typename TKey, typename TValue>
	void WriteOnly(TKey key, TValue value, typename std::enable_if<std::is_fundamental<TValue>::value >::type* dummy = 0)
	{
		if(IsWriter)
			Write(key, value);
	}
	
	//Serialize a series of items by start and end iterators
	template<typename TKey, typename TItor>
	void WriteOnly(TKey key, TItor first, TItor last)
	{
		if(!IsWriter)
			return;
		
		JsonSerializer subVal(IsWriter);
		int index = 0;
		subVal.JsonValue = Json::arrayValue;
		for(TItor it = first; it != last; ++it)
		{
			subVal.Serialize(index, *it);
			++index;
		}
		JsonValue[key] = subVal.JsonValue;
	}
	
	template<typename TKey, typename TValue>
	void ReadOnly(TKey key, TValue& value, typename std::enable_if<std::is_fundamental<TValue>::value >::type* dummy = 0)
	{
		if(!IsWriter)
			Read(key, value);
	}

	template<typename TValue>
	void ReadOnly(std::vector<TValue>& vec)
	{
		if(IsWriter)
			return;
		if(!JsonValue.isArray())
			return;
		
		vec.clear();
		vec.reserve(vec.size() + JsonValue.size());
		for(int i = 0; i < JsonValue.size(); ++i)
		{
			TValue val;
			Serialize(i, val);
			vec.push_back(val);
		}
	}
	
	template<typename TKey, typename TValue>
	void Serialize(TKey key, std::vector<TValue>& vec)
	{
		if(IsWriter)
		{
			WriteOnly(key, vec.begin(), vec.end());
		}
		else
		{
			JsonSerializer subVal(IsWriter);
			subVal.JsonValue = JsonValue[key];
			subVal.ReadOnly(vec);
		}
	}
	
	template<typename TValue>
	void ReadOnly(std::deque<TValue>& dq)
	{
		if(IsWriter)
			return;
		if(!JsonValue.isArray())
			return;
		
		dq.clear();
		for(int i = 0; i < JsonValue.size(); ++i)
		{
			TValue val;
			Serialize(i, val);
			dq.push_back(val);
		}
	}
	
	template<typename TKey, typename TValue>
	void Serialize(TKey key, std::deque<TValue>& dq)
	{
		if(IsWriter)
		{
			WriteOnly(key, dq.begin(), dq.end());
		}
		else
		{
			JsonSerializer subVal(IsWriter);
			subVal.JsonValue = JsonValue[key];
			subVal.ReadOnly(dq);
		}
	}
	
	template<typename TValue>
	void ReadOnly(std::map<std::string, TValue>& m)
	{
		if(IsWriter)
			return;
		if(!JsonValue.isObject())
			return;

		m.clear();
		for(auto it = JsonValue.begin(); it != JsonValue.end(); ++it)
		{
			JsonSerializer subVal(IsWriter);
			subVal.JsonValue = *it;
			std::string key = it.key().asString();
			TValue value;
			Serialize(key, value);
			m[key] = value;
		}
	}

	template<typename TKey, typename TValue>
	void WriteOnly(TKey key, std::map<std::string, TValue>& m)
	{
		if(!IsWriter)
			return;
		
		JsonSerializer subVal(IsWriter);
		for(auto it = m.begin(); it != m.end(); ++it)
		{
			subVal.Serialize(it->first, it->second);
		}
		JsonValue[key] = subVal.JsonValue;
	}

	template<typename TKey, typename TValue>
	void Serialize(TKey key, std::map<std::string, TValue>& m)
	{
		if(IsWriter)
		{
			WriteOnly(key, m);
		}
		else
		{
			JsonSerializer subVal(IsWriter);
			subVal.JsonValue = JsonValue[key];
			subVal.ReadOnly(m);
		}
	}

	//Append a Json::Value directly
	template<typename TKey>
	void WriteOnly(TKey key, const Json::Value& value)
	{
		Write(key, value);
	}
	
	//Forward a pointer
	template<typename TKey, typename TValue>
	void Serialize(TKey key, TValue* value, typename std::enable_if<!std::is_fundamental<TValue>::value >::type* dummy = 0)
	{
		Serialize(key, *value);
	}
	
	template<typename TKey, typename TValue>
	void WriteOnly(TKey key, TValue* value, typename std::enable_if<!std::is_fundamental<TValue>::value >::type* dummy = 0)
	{
		Serialize(key, *value);
	}
	
	template<typename TKey, typename TValue>
	void ReadOnly(TKey key, TValue* value, typename std::enable_if<!std::is_fundamental<TValue>::value >::type* dummy = 0)
	{
		ReadOnly(key, *value);
	}
	
	//intellisense hack, doesn't do anything
	template<typename T>
	void SerializeNVP(T name)
	{
	}

	//Shorthand operator to serialize
	template<typename TKey, typename TValue>
	void operator()(TKey key, TValue&& value)
	{
		Serialize(key, value);
	}
	
	Json::Value JsonValue;
	bool IsWriter;
	
private:
	template<typename TKey, typename TValue>
	void Write(TKey key, TValue value)
	{
		JsonValue[key] = value;
	}
	
	template<typename TKey>
	void Write(TKey key, long value)
	{
		JsonValue[key] = (int) value;
	}
	
	template<typename TKey>
	void Write(TKey key, unsigned long value)
	{
		JsonValue[key] = (unsigned int) value;
	}
	
	template<typename TKey>
	void Write(TKey key, size_t value)
	{
		JsonValue[key] = (unsigned int)value;
	}

	template<typename TKey, typename TValue>
	void Read(TKey key, TValue& value, typename std::enable_if<std::is_arithmetic<TValue>::value >::type* dummy = 0)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		int ival = subval.asInt();
		value = (TValue) ival;
	}
	
	template<typename TKey>
	void Read(TKey key, bool& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asBool();
	}
	
	template<typename TKey>
	void Read(TKey key, int& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asInt();
	}
	
	template<typename TKey>
	void Read(TKey key, long& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asInt();
	}
	
	template<typename TKey>
	void Read(TKey key, unsigned int& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asUInt();
	}
	
	template<typename TKey>
	void Read(TKey key, unsigned long& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asUInt();
	}

	/*template<typename TKey>
	void Read(TKey key, size_t& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asUInt();
	}*/

	template<typename TKey>
	void Read(TKey key, float& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asFloat();
	}
	
	template<typename TKey>
	void Read(TKey key, double& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asDouble();
	}
	
	template<typename TKey>
	void Read(TKey key, std::string& value)
	{
		Json::Value subval = JsonValue[key];
		if(subval.isNull())
			return;
		value = subval.asString();
	}
};

//"name value pair", derived from boost::serialization terminology
#define NVP(name) #name, name
#define SerializeNVP(name) Serialize(NVP(name))

#endif
