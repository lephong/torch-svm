
#include "TH.h"
#include "luaT.h"

#include "lualib.h"

#define max_(a,b) (a>=b ? a : b)
#define min_(a,b) (a<=b ? a : b)


static int svm_readbinary(lua_State *L)
{
	int normindex = 2;
	int maxrows = -1;

	// read the file name or the file pointer
	int ownfile = 1;
	const char *fname = lua_tostring(L,1);
	FILE *fp;
	if (fname == NULL)
	{
		fp = (*(FILE **)luaL_checkudata(L, 1, LUA_FILEHANDLE));
		ownfile = 0;
		// check if next entry is a number, then use it as number of 
		// samples to read
		if (lua_isnumber(L,2))
		{
			maxrows = (int)lua_tonumber(L,2);
			normindex = 3;
		}
	}
	else
	{
		fp = fopen(fname,"r");
		printf("Reading %s\n",fname);
	}

	luaL_argcheck(L, fp != NULL, 1, "File could not be opened");

	// 

	int normalize = 0;
	
	if lua_isnil(L,normindex)
		normalize = 1;
	else if lua_isboolean(L,normindex)
		normalize = lua_toboolean(L,normindex);

	// printf("norm=%d nil=%d bool=%d \n",normalize,lua_isnil(L,2),lua_isboolean(L,2));

	char y;
	int nf;
	int i;
	lua_newtable(L);
	int cntr = 1;
	int npos = 0;
	int nneg = 0;
	int maxdim = 0;
	int minsparse = INT_MAX;
	int maxsparse = 0;
	while (maxrows-- && fread((void*)&y,sizeof(char),1,fp))
	{
		fread((void*)&nf,sizeof(int),1,fp);
		THIntTensor *indices = THIntTensor_newWithSize1d(nf);
		THFloatTensor *vals = THFloatTensor_newWithSize1d(nf);
		int *indices_data = THIntTensor_data(indices);
		float *vals_data = THFloatTensor_data(vals);
		for (i=0; i<nf; i++)
		{
			fread((void*)indices_data++,sizeof(int),1,fp);
			fread((void*)vals_data++,sizeof(float),1,fp);
		}
		if (normalize)
			THFloatTensor_div(vals,vals,THFloatTensor_normall(vals,2));

		if (y>0)
			npos += 1;
		else
			nneg += 1;

		maxdim = max_(maxdim,indices_data[-1]);
		minsparse = min_(minsparse,nf);
		maxsparse = max_(maxsparse,nf);

		lua_newtable(L);
		{
			lua_pushnumber(L,(y ? 1 : -1));
			lua_rawseti(L,-2,1);
			lua_newtable(L);
			{
				luaT_pushudata(L,indices,"torch.IntTensor");
				lua_rawseti(L,-2,1);
				luaT_pushudata(L,vals,"torch.FloatTensor");
				lua_rawseti(L,-2,2);
			}
			lua_rawseti(L,-2,2);
		}
		lua_rawseti(L,-2,cntr);
		cntr++;
	}
	cntr--;
	if (ownfile)
	{
		fclose(fp);
	}
	if (maxrows < -1)
	{
		printf("# of positive samples = %d\n",npos);
		printf("# of negative samples = %d\n",nneg);
		printf("# of total    samples = %d\n",cntr);
		printf("# of max dimensions   = %d\n",maxdim);
		printf("Min # of dims = %d\n",minsparse);
		printf("Max # of dims = %d\n",maxsparse);
		lua_pushnumber(L,(double)maxdim);
		return 2;
	}
	return 1;
}

static int svm_infobinary(lua_State *L)
{
	// read the file name or the file pointer
	const char *fname = lua_tostring(L,1);
	FILE *fp = fopen(fname,"r");
	printf("Reading %s\n",fname);

	luaL_argcheck(L, fp != NULL, 1, "File could not be opened");

	char y;
	int nf;
	int cntr = 1;
	int npos = 0;
	int nneg = 0;
	int maxdim = 0;
	while (fread((void*)&y,sizeof(char),1,fp))
	{
		if (y>0)
			npos += 1;
		else
			nneg += 1;

		fread((void*)&nf,sizeof(int),1,fp);
		fseek(fp,(nf-1)*2*4,SEEK_CUR);
		fread((void*)&nf,sizeof(int),1,fp);
		fseek(fp,4,SEEK_CUR);
		maxdim = max_(maxdim,nf);
		cntr++;
	}
	cntr--;
	fclose(fp);
	printf("# of positive samples = %d\n",npos);
	printf("# of negative samples = %d\n",nneg);
	printf("# of total    samples = %d\n",cntr);
	printf("# of max dimensions   = %d\n",maxdim);
	lua_pushnumber(L,(double)cntr);
	lua_pushnumber(L,(double)maxdim);
	return 2;
}

static int svm_spdot(lua_State *L)
{
	THFloatTensor *tdense = luaT_checkudata(L,1,"torch.FloatTensor");
	THIntTensor *indices;
	if lua_isnil(L,2)
	{
		indices = NULL;
	}
	else
	{
		indices = luaT_checkudata(L,2,"torch.IntTensor");
	}
	THFloatTensor *tsparse = luaT_checkudata(L,3,"torch.FloatTensor");

	luaL_argcheck(L,tdense->nDimension == 1, 1, "Dense Matrix is expected to 1D");
	luaL_argcheck(L,!indices || indices->nDimension == 1, 2, "Index tensor is expected to 1D");
	luaL_argcheck(L,tsparse->nDimension == 1, 3, "Sparse value tensor is expected to 1D");

	if (!indices)
	{
		lua_pushnumber(L,(double)THFloatTensor_dot(tdense,tsparse));
		return 1;
	}

	float *dense_data = THFloatTensor_data(tdense);
	float *sparse_data = THFloatTensor_data(tsparse);
	int *indices_data = THIntTensor_data(indices);

	long i;
	float res = 0;

	for (i=0; i< indices->size[0]; i++)
	{
		res += sparse_data[i]*dense_data[indices_data[i]-1];
	}
	lua_pushnumber(L,(double)res);
	return 1;
}

static int svm_spadd(lua_State *L)
{
	THFloatTensor *tdense = luaT_checkudata(L,1,"torch.FloatTensor");
	float c = (float)lua_tonumber(L,2);
	THIntTensor *indices;
	if (lua_isnil(L,3))
	{
		indices = NULL;
	}
	else
	{
		indices = luaT_checkudata(L,3,"torch.IntTensor");
	}
	THFloatTensor *tsparse = luaT_checkudata(L,4,"torch.FloatTensor");

	luaL_argcheck(L,tdense->nDimension == 1, 1, "Dense Matrix is expected to 1D");
	luaL_argcheck(L,!indices||indices->nDimension == 1, 3, "Index tensor is expected to 1D");
	luaL_argcheck(L,tsparse->nDimension == 1, 4, "Sparse value tensor is expected to 1D");

	if(!indices)
	{
		THFloatTensor_cadd(tdense,tdense,c,tsparse);
		return 0;
	}

	float *dense_data = THFloatTensor_data(tdense);
	float *sparse_data = THFloatTensor_data(tsparse);
	int *indices_data = THIntTensor_data(indices);

	long i;

	for (i=0; i< indices->size[0]; i++)
	{
		dense_data[indices_data[i]-1] += c*sparse_data[i];
	}
	return 0;
}

static const struct luaL_Reg svm_util__ [] = {
  {"spdot", svm_spdot},
  {"spadd", svm_spadd},
  {"binread", svm_readbinary},
  {"bininfo", svm_infobinary},
  {NULL, NULL}
};


DLL_EXPORT int luaopen_libsvm(lua_State *L)
{
  luaL_register(L, "svm", svm_util__);
  return 1;
}
