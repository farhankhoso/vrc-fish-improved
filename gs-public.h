using namespace std;

// 字符串分割
vector<string> split(const string& str, const string& pattern) {
	vector<string> ret;
	if (pattern.empty()) return ret;
	size_t start = 0, index = str.find_first_of(pattern, 0);
	while (index != str.npos)
	{
		if (start != index)
			ret.push_back(str.substr(start, index - start));
		start = index + 1;
		index = str.find_first_of(pattern, start);
	}
	if (!str.substr(start).empty())
		ret.push_back(str.substr(start));
	return ret;
}

// 读取注册表
string getRegValue(HKEY lpMainKey, LPCTSTR lpSubKey, LPCTSTR lpValueName) {
	bool bRet = false;
	string strReturn;
	HKEY hKey;
	if (ERROR_SUCCESS == ::RegOpenKeyEx(lpMainKey, lpSubKey, 0, KEY_READ, &hKey))
	{
		DWORD dwType = REG_SZ;
		TCHAR szReturnValue[MAX_PATH + 1] = { 0 };
		DWORD dwSize = MAX_PATH;
		if (ERROR_SUCCESS == ::RegQueryValueEx(hKey, lpValueName, NULL, &dwType, (LPBYTE)szReturnValue, &dwSize))
		{
			// 将宽字节转换成多字节
			int dwBufSize = ::WideCharToMultiByte(CP_OEMCP, 0, szReturnValue, -1, NULL, 0, NULL, FALSE);
			char* buff = new char[dwBufSize];
			memset(buff, 0, dwBufSize);
			if (::WideCharToMultiByte(CP_OEMCP, 0, szReturnValue, -1, buff, dwBufSize, NULL, FALSE))
			{
				strReturn = buff;
				delete[] buff;
				buff = NULL;
			}
			bRet = true;
		}
	}
	::RegCloseKey(hKey);
	if (!bRet) {
		cout << "读取注册表失败" << endl;
		system("pause");
		exit(0);
	}
	return strReturn;
}

// 四舍五入
int roundNum(double number) {
	return (number > 0.0) ? (number + 0.5) : (number - 0.5);
}

// 读取 REG_DWORD 类型的注册表键值代码
DWORD getRegDwordValue(HKEY lpMainKey, LPCTSTR lpSubKey, LPCTSTR lpValueName) {
	bool bRet = false;
	long lRet;
	HKEY hKey;
	DWORD result;
	DWORD dwType = REG_DWORD;
	DWORD dwValue = MAX_PATH;
	lRet = RegOpenKeyEx(lpMainKey, lpSubKey, 0, KEY_QUERY_VALUE, &hKey);
	//打开注册表
	if (ERROR_SUCCESS == RegOpenKeyEx(lpMainKey, lpSubKey, 0, KEY_QUERY_VALUE, &hKey))//读操作成功
	{
		//如果打开成功，则读
		if (ERROR_SUCCESS == RegQueryValueEx(hKey, lpValueName, 0, &dwType, (LPBYTE)&result, &dwValue))
		{
			bRet = true;
		}
	}
	RegCloseKey(hKey);//记住，一定要关闭
	if (!bRet) {
		cout << "读取注册表失败" << endl;
		system("pause");
		exit(0);
	}
	return result;
}

// 退出程序
void exit() {
	system("pause");
	exit(0);
}

//获取时间戳函数（秒）
long long get_timestamp() {
	struct timespec ts {};
	long long t = timespec_get(&ts, TIME_UTC);
	return ts.tv_sec;
}

// 获取时间戳距离现在的时长（秒）
long long get_subTimestamp(long long t1) {
	long long t2 = get_timestamp();
	return t2 - t1;
}

bool isNumeric(std::string const& str) {
	auto it = str.begin();
	while (it != str.end() && std::isdigit(*it)) {
		it++;
	}
	return !str.empty() && it == str.end();
}

// 读取键值
int getKeyVal(string s) {
	if (s.size() > 2 && s.substr(0, 2) == "0x") {
		return std::stoi(s, nullptr, 16);
	}
	if (s.length() == 1) {
		if (isNumeric(s)) {
			if (s.length() == 1) {
				return 0x30 + atoi(s.c_str());
			}
		}
		else {
			return s[0];
		}
	}
	else if (s.compare("leftClick") == 0) return 0x01;
	else if (s.compare("rightClick") == 0) return 0x02;
	else if (s.compare("ctrl") == 0) return 17;
	else if (s.compare("space") == 0) return 32;
	else if (s.compare("tab") == 0) return 9;
	else {
		std::cout << "键码识别错误：" + s;
		exit(0);
		system("pause");
	}
}