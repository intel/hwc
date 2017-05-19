/*
// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include "LogicalDisplay.h"
#include "Log.h"

namespace intel {
namespace ufo {
namespace hwc {

std::vector<LogicalDisplay::Factory*> LogicalDisplay::sLogicalDisplayVec;

LogicalDisplay::LogicalDisplay( Hwc& hwc, LogicalDisplayManager& ldm, PhysicalDisplayManager& pdm, ELogicalType eType ) :
        mHwc( hwc ),
        mLogicalDisplayManager( ldm ),
        mPhysicalDisplayManager( pdm ),
        mSfIndex( INVALID_DISPLAY_ID ),
        mDmIndex( 0 ),
        meLogicalType( eType ),
        mSizeWidth( 0 ),
        mSizeHeight( 0 ),
        mTag( "???" ),
        mbProxyOnly( false )
{
}

LogicalDisplay::~LogicalDisplay()
{
}
LogicalDisplay::ELogicalType LogicalDisplay::getLogicalType( void ) const
{
    return meLogicalType;
}

void LogicalDisplay::setTag( const char* pchTag )
{
    mTag = pchTag;
}

const String8& LogicalDisplay::getTag( void ) const
{
    return mTag;
}

const char* LogicalDisplay::getTagStr( void ) const
{
    return mTag.string();
}

void LogicalDisplay::setSize( uint32_t w, uint32_t h )
{
    mSizeWidth = w;
    mSizeHeight = h;
}

uint32_t LogicalDisplay::getSizeWidth( void  ) const
{
    return mSizeWidth;
}

uint32_t LogicalDisplay::getSizeHeight( void  ) const
{
    return mSizeHeight;
}

void LogicalDisplay::setSurfaceFlingerIndex( uint32_t id)
{
    mSfIndex = id;
}

uint32_t LogicalDisplay::getSurfaceFlingerIndex() const
{
    return mSfIndex;
}

bool LogicalDisplay::isPluggedToSurfaceFlinger( void ) const
{
    return ( getSurfaceFlingerIndex() != INVALID_DISPLAY_ID );
}

void LogicalDisplay::setDisplayManagerIndex( uint32_t dmIndex )
{
    mDmIndex = dmIndex;
}

uint32_t LogicalDisplay::getDisplayManagerIndex( void ) const
{
    return mDmIndex;
}

void LogicalDisplay::add(Factory* instance)
{
    if(instance != NULL)
        sLogicalDisplayVec.push_back (instance);
}

void LogicalDisplay::remove(Factory* instance)
{
    if(instance != NULL)
    {
        sLogicalDisplayVec.erase( std::remove( sLogicalDisplayVec.begin(), sLogicalDisplayVec.end(), instance ), sLogicalDisplayVec.end() );
    }
}

LogicalDisplay* LogicalDisplay::instantiate(const char* pchConfig,
                                        Hwc& hwc,
                                        LogicalDisplayManager& ldm,
                                        PhysicalDisplayManager& pdm,
                                        const int32_t sfIndex,
                                        EIndexType eIndexType,
                                        int32_t phyIndex,
                                        EDisplayType eType)
{
    LogicalDisplay* pBaseDisplay = NULL;
    for( auto pReg: sLogicalDisplayVec )
    {
        pBaseDisplay = pReg->create( pchConfig, hwc, ldm, pdm,
                                        sfIndex,
                                        eIndexType,
                                        phyIndex,
                                        eType);
        if(pBaseDisplay)
            break;
    }
    return pBaseDisplay;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

