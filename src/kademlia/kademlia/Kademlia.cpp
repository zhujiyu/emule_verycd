/*
Copyright (C)2003 Barry Dunne (http://www.emule-project.net)
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// Note To Mods //
/*
Please do not change anything here and release it..
There is going to be a new forum created just for the Kademlia side of the client..
If you feel there is an error or a way to improve something, please
post it in the forum first and let us look at it.. If it is a real improvement,
it will be added to the offical client.. Changing something without knowing
what all it does can cause great harm to the network if released in mass form..
Any mod that changes anything within the Kademlia side will not be allowed to advertise
there client on the eMule forum..
*/

#include "stdafx.h"
#include "./Kademlia.h"
#include "./defines.h"
#include "./Prefs.h"
#include "./SearchManager.h"
#include "./Indexed.h"
#include "./UDPFirewallTester.h"
#include "../net/KademliaUDPListener.h"
#include "../routing/RoutingZone.h"
#include "../routing/contact.h"
#include "../../emule.h"
#include "../../preferences.h"
#include "../../emuledlg.h"
#include "../../opcodes.h"
#include "../../Log.h"
#include "../../MD4.h"
#include "../../StringConversion.h"
#include "../utils/KadUDPKey.h"
#include "../utils/KadClientSearcher.h"

//  added by yunchenn
#include "../../ClientList.h"
#include "Search.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace Kademlia;

CKademlia	*CKademlia::m_pInstance = NULL;
EventMap	CKademlia::m_mapEvents;
time_t		CKademlia::m_tNextSearchJumpStart;
time_t		CKademlia::m_tNextSelfLookup;
time_t		CKademlia::m_tStatusUpdate;
time_t		CKademlia::m_tBigTimer;
time_t		CKademlia::m_tNextFirewallCheck;
time_t		CKademlia::m_tNextFindBuddy;
time_t		CKademlia::m_tBootstrap;
time_t		CKademlia::m_tConsolidate;
time_t		CKademlia::m_tExternPortLookup;
time_t		CKademlia::m_tRecvData=0;
bool		CKademlia::m_bRunning = false;
CList<uint32, uint32> CKademlia::m_liStatsEstUsersProbes;
_ContactList CKademlia::s_liBootstapList;

CKademlia::CKademlia()
{}

void CKademlia::Start()
{
	// Create a new default pref object.
	Start(new CPrefs());
}

void CKademlia::Start(CPrefs *pPrefs)
{
	try
	{
		// If we already have a instance, something is wrong. 
		if( m_pInstance )
		{
			delete pPrefs;
			ASSERT(m_pInstance->m_bRunning);
			ASSERT(m_pInstance->m_pPrefs);
			return;
		}

		// Make sure a prefs was passed in..
		if( !pPrefs )
			return;

		AddDebugLogLine(false, _T("Starting Kademlia"));

		m_tRecvData = time(NULL);

		// Init jump start timer.
		m_tNextSearchJumpStart = time(NULL);

		m_tNextUpdateWholeKBucket = time(NULL);	//ADDED by VC-fengwen 2007/08/27 : restart后重新更新K桶，以避免upnp成功后restart kad，中断了更新K桶的操作。
		//MODIFIED by VC-fengwen 2007/08/13 <begin> : 引入快速填充K桶机制，FindNodeComplete在Process_UpdateWholeKBucket之后即可。
		// Force a FindNodeComplete within the first 3 minutes.
		//m_tNextSelfLookup = time(NULL) + MIN2S(3);
		m_tNextSelfLookup = time(NULL) /*+ MIN2S(3)*/;
		//MODIFIED by VC-fengwen 2007/08/13 <end> : 引入快速填充K桶机制，FindNodeComplete在Process_UpdateWholeKBucket之后即可。
		// Init status timer.
		m_tStatusUpdate = time(NULL);
		// Init big timer for Zones
		m_tBigTimer = time(NULL);
		// First Firewall check is done on connect, init next check.
		m_tNextFirewallCheck = time(NULL) + (HR2S(1));
		// Find a buddy after the first 5mins of starting the client.
		// We wait just in case it takes a bit for the client to determine firewall status..
		m_tNextFindBuddy = time(NULL) + (MIN2S(5));
		// Init contact consolidate timer;
		m_tConsolidate = time(NULL) + (MIN2S(45));
		// Looking up our extern port
		m_tExternPortLookup = time(NULL);
		// Init bootstrap time.
		m_tBootstrap = 0;
		// Init our random seed.
		srand((UINT)time(NULL));
		// Create our Kad objects.
		m_pInstance = new CKademlia();
		m_pInstance->m_pPrefs = pPrefs;
		m_pInstance->m_pUDPListener = NULL;
		m_pInstance->m_pRoutingZone = NULL;
		m_pInstance->m_pIndexed = new CIndexed();
		m_pInstance->m_pRoutingZone = new CRoutingZone();
		m_pInstance->m_pUDPListener = new CKademliaUDPListener();
		// Mark Kad as running state.
		m_bRunning = true;
	}
	catch (CException *e)
	{
		// Although this has never been an issue, maybe some just in case code
		// needs to be created here just in case things go real bad.. But if things
		// went real bad, the entire client most like is in bad shape, so this may
		// not be something to worry about as the client most likely will crap out anyway.
		TCHAR err[512];
		e->GetErrorMessage(err, 512);
		AddDebugLogLine( false, _T("%s"), err);
		e->Delete();
	}
}

void CKademlia::Stop()
{
	// Make sure we are running to begin with.
	if( !m_bRunning )
		return;

	AddDebugLogLine(false, _T("Stopping Kademlia"));

	// Mark Kad as being in the stop state to make sure nothing else is used.
	m_bRunning = false;

	// Reset Firewallstate
	CUDPFirewallTester::Reset();

	// Remove all active searches.
	CSearchManager::StopAllSearches();

	// Delete all Kad Objects.
	delete m_pInstance->m_pUDPListener;
	m_pInstance->m_pUDPListener = NULL;

	delete m_pInstance->m_pRoutingZone;
	m_pInstance->m_pRoutingZone = NULL;

	delete m_pInstance->m_pIndexed;
	m_pInstance->m_pIndexed = NULL;

	delete m_pInstance->m_pPrefs;
	m_pInstance->m_pPrefs = NULL;

	delete m_pInstance;
	m_pInstance = NULL;

	while (!s_liBootstapList.IsEmpty())
		delete s_liBootstapList.RemoveHead();

	// Make sure all zones are removed.
	m_mapEvents.clear();
}

void CKademlia::Process()
{
	if( m_pInstance == NULL || !m_bRunning)
		return;
	bool bUpdateUserFile = false;
	uint32 uMaxUsers = 0;
	uint32 uTempUsers = 0;
	uint32 uLastContact = 0;
	time_t tNow = time(NULL);
	ASSERT(m_pInstance->m_pPrefs != NULL);
	uLastContact = m_pInstance->m_pPrefs->GetLastContact();
	CSearchManager::UpdateStats();
	if( m_tStatusUpdate <= tNow )
	{
		bUpdateUserFile = true;
		m_tStatusUpdate = MIN2S(1) + tNow;
	}
	if( m_tNextFirewallCheck <= tNow)
		RecheckFirewalled();

	//ADDED by VC-fengwen 2007/08/08 <begin> :
	// Process_UpdateWholeKBucket 应放在Find self 之前，因为Find self 会带上NodeComplete标记以表示节点更新完毕。
	Process_UpdateWholeKBucket();
	//ADDED by VC-fengwen 2007/08/08 <end> :
	
	if (m_tNextSelfLookup <= tNow)
	{
		CSearchManager::FindNode(m_pInstance->m_pPrefs->GetKadID(), true);
		m_tNextSelfLookup = HR2S(4) + tNow;
	}
	if (m_tNextFindBuddy <= tNow)
	{
		m_pInstance->m_pPrefs->SetFindBuddy();
#ifdef _DEBUG
		m_tNextFindBuddy = MIN2S(5) + tNow;
#else
		m_tNextFindBuddy = MIN2S(8) + tNow;
#endif
	}
	if (m_tExternPortLookup <= tNow && CUDPFirewallTester::IsFWCheckUDPRunning() && GetPrefs()->GetExternalKadPort() == 0){
		// if our UDP firewallcheck is running and we don't know our external port, we send a request every 15 seconds
		CContact* pContact = GetRoutingZone()->GetRandomContact(3, KADEMLIA_VERSION6_49aBETA);
		if (pContact != NULL){
			DEBUG_ONLY( DebugLog(_T("Requesting our external port from %s"), ipstr(ntohl(pContact->GetIPAddress()))) );
			GetUDPListener()->SendNullPacket(KADEMLIA2_PING, pContact->GetIPAddress(), pContact->GetUDPPort(), pContact->GetUDPKey(), &pContact->GetClientID());
		}
		else
			DEBUG_ONLY( DebugLogWarning(_T("No valid client for requesting external port available")) );
		m_tExternPortLookup = 15 + tNow;
	}
	for (EventMap::const_iterator itEventMap = m_mapEvents.begin(); itEventMap != m_mapEvents.end(); ++itEventMap)
	{
		CRoutingZone* pZone = itEventMap->first;
		if( bUpdateUserFile )
		{
			uTempUsers = pZone->EstimateCount();
			if( uMaxUsers < uTempUsers )
				uMaxUsers = uTempUsers;
		}
		if (m_tBigTimer <= tNow)
		{
			if( pZone->m_tNextBigTimer <= tNow )
			{
				if(pZone->OnBigTimer())
				{
					pZone->m_tNextBigTimer = HR2S(1) + tNow;
					m_tBigTimer = SEC(10) + tNow;
				}
			}
			else
			{
				if( uLastContact && ( (tNow - uLastContact) > (KADEMLIADISCONNECTDELAY-MIN2S(5))))
				{
					if(pZone->OnBigTimer())
					{
						pZone->m_tNextBigTimer = HR2S(1) + tNow;
						m_tBigTimer = SEC(10) + tNow;
					}
				}
			}
		}
		if (pZone->m_tNextSmallTimer <= tNow)
		{
			pZone->OnSmallTimer();
			pZone->m_tNextSmallTimer = MIN2S(1) + tNow;
		}
	}

	// This is a convenient place to add this, although not related to routing
	if (m_tNextSearchJumpStart <= tNow)
	{
		CSearchManager::JumpStart();
		m_tNextSearchJumpStart = SEARCH_JUMPSTART + tNow;
	}

	// Try to consolidate any zones that are close to empty.
	if (m_tConsolidate <= tNow)
	{
		uint32 uMergedCount = m_pInstance->m_pRoutingZone->Consolidate();
		if(uMergedCount)
			AddDebugLogLine(false, _T("Kad merged %u Zones"), uMergedCount);
		m_tConsolidate = MIN2S(45) + tNow;
	}

	//Update user count only if changed.
	if( bUpdateUserFile )
	{
		if( uMaxUsers != m_pInstance->m_pPrefs->GetKademliaUsers())
		{
			m_pInstance->m_pPrefs->SetKademliaUsers(uMaxUsers);
			m_pInstance->m_pPrefs->SetKademliaFiles();
			theApp.emuledlg->ShowUserCount();
		}
	}

	if(!IsConnected() && !s_liBootstapList.IsEmpty() 
		&& (tNow - m_tBootstrap > 15 || (GetRoutingZone()->GetNumContacts() == 0 && tNow - m_tBootstrap >= 2)))
	{
		CContact* pContact = s_liBootstapList.RemoveHead();
		m_tBootstrap = tNow;
		DebugLog(_T("Trying to Bootstrap Kad from %s, Distance: %s, Version: %u, %u Contacts left"), ipstr(ntohl(pContact->GetIPAddress())), pContact->GetDistance().ToHexString(),  pContact->GetVersion(), s_liBootstapList.GetCount());
		m_pInstance->m_pUDPListener->Bootstrap(pContact->GetIPAddress(), pContact->GetUDPPort(), pContact->GetVersion() > 1, pContact->GetVersion(), &pContact->GetClientID());
		delete pContact;
	}

	if (GetUDPListener() != NULL)
		GetUDPListener()->ExpireClientSearch(); // function does only one compare in most cases, so no real need for a timer
}

void CKademlia::AddEvent(CRoutingZone *pZone)
{
	m_mapEvents[pZone] = pZone;
}

void CKademlia::RemoveEvent(CRoutingZone *pZone)
{
	m_mapEvents.erase(pZone);
}

bool CKademlia::IsConnected()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->HasHadContact();
	return false;
}

bool CKademlia::IsFirewalled()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetFirewalled();
	return true;
}

//  added by yunchenn 2006.12.13
bool CKademlia::StartFindBuddy()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
	{
		//TODO: Kad buddies won'T work with RequireCrypt, so it is disabled for now but should (and will)
		//be fixed in later version

		//  force finding buddy here
		Kademlia::CKademlia::GetPrefs()->SetFindBuddy();

		if( theApp.clientlist->GetBuddyStatus()== Disconnected && Kademlia::CKademlia::GetPrefs()
			&& Kademlia::CKademlia::GetPrefs()->GetFindBuddy() && !thePrefs.IsClientCryptLayerRequired())
		{
			//We are a firewalled client with no buddy. We have also waited a set time 
			//to try to avoid a false firewalled status.. So lets look for a buddy..
#ifdef _DEBUG_KAD_
			Debug( _T("Now PrepareLookup FINDBUDDY! \r\n") );
#endif 
			if( !Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::FINDBUDDY, true, Kademlia::CUInt128(true).Xor(Kademlia::CKademlia::GetPrefs()->GetKadID())) )
			{
				//This search ID was already going. Most likely reason is that
				//we found and lost our buddy very quickly and the last search hadn't
				//had time to be removed yet. Go ahead and set this to happen again
				//next time around.
				Kademlia::CKademlia::GetPrefs()->SetFindBuddy();
				return true;
			}
		}
	}
	return false;
}

uint32 CKademlia::GetKademliaUsers(bool bNewMethod)
{
	if( m_pInstance && m_pInstance->m_pPrefs ){
		if (bNewMethod)
			return CalculateKadUsersNew();
		else
		return m_pInstance->m_pPrefs->GetKademliaUsers();
	}
	return 0;
}

uint32 CKademlia::GetKademliaFiles()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetKademliaFiles();
	return 0;
}

uint32 CKademlia::GetTotalStoreKey()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetTotalStoreKey();
	return 0;
}

uint32 CKademlia::GetTotalStoreSrc()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetTotalStoreSrc();
	return 0;
}

uint32 CKademlia::GetTotalStoreNotes()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetTotalStoreNotes();
	return 0;
}

uint32 CKademlia::GetTotalFile()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetTotalFile();
	return 0;
}

uint32 CKademlia::GetIPAddress()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetIPAddress();
	return 0;
}

void CKademlia::ProcessPacket(const byte *pbyData, uint32 uLenData, uint32 uIP, uint16 uPort, bool bValidReceiverKey, CKadUDPKey senderUDPKey)
{
	m_tRecvData = time(NULL);

	if( m_pInstance && m_pInstance->m_pUDPListener )
		m_pInstance->m_pUDPListener->ProcessPacket( pbyData, uLenData, uIP, uPort, bValidReceiverKey, senderUDPKey);
}

bool CKademlia::GetPublish()
{
	if( m_pInstance && m_pInstance->m_pPrefs )
		return m_pInstance->m_pPrefs->GetPublish();
	return 0;
}

void CKademlia::Bootstrap(LPCTSTR szHost, uint16 uPort, bool bKad2)
{
	if( m_pInstance && m_pInstance->m_pUDPListener && !IsConnected() && time(NULL) - m_tBootstrap > 10 ){
		m_tBootstrap = time(NULL);
		m_pInstance->m_pUDPListener->Bootstrap( szHost, uPort, bKad2 );
}
}

void CKademlia::Bootstrap(uint32 uIP, uint16 uPort, bool bKad2)
{
	if( m_pInstance && m_pInstance->m_pUDPListener && !IsConnected() && time(NULL) - m_tBootstrap > 10 ){
		m_tBootstrap = time(NULL);
		m_pInstance->m_pUDPListener->Bootstrap( uIP, uPort, bKad2 );
}
}

void CKademlia::RecheckFirewalled()
{
	if( m_pInstance && m_pInstance->GetPrefs() )
	{
		// Something is forcing a new firewall check
		// Stop any new buddy requests, and tell the client
		// to recheck it's IP which in turns rechecks firewall.
		m_pInstance->m_pPrefs->SetFindBuddy(false);
		m_pInstance->m_pPrefs->SetRecheckIP();
		// also UDP check
		CUDPFirewallTester::ReCheckFirewallUDP(false);
		
		time_t tNow = time(NULL);
		// Delay the next buddy search to at least 5 minutes after our firewallcheck so we are sure to be still firewalled
		m_tNextFindBuddy = (m_tNextFindBuddy < MIN2S(5) + tNow) ?  (MIN2S(5) + tNow) : m_tNextFindBuddy;
		m_tNextFirewallCheck = HR2S(1) + tNow;
	}
}

CPrefs *CKademlia::GetPrefs()
{
	if (m_pInstance == NULL || m_pInstance->m_pPrefs == NULL)
	{
		//ASSERT(0);
		return NULL;
	}
	return m_pInstance->m_pPrefs;
}

CKademliaUDPListener *CKademlia::GetUDPListener()
{
	if (m_pInstance == NULL || m_pInstance->m_pUDPListener == NULL)
	{
		ASSERT(0);
		return NULL;
	}
	return m_pInstance->m_pUDPListener;
}

CRoutingZone *CKademlia::GetRoutingZone()
{
	if (m_pInstance == NULL || m_pInstance->m_pRoutingZone == NULL)
	{
		ASSERT(0);
		return NULL;
	}
	return m_pInstance->m_pRoutingZone;
}

CIndexed *CKademlia::GetIndexed()
{
	if ( m_pInstance == NULL || m_pInstance->m_pIndexed == NULL)
	{
		ASSERT(0);
		return NULL;
	}
	return m_pInstance->m_pIndexed;
}

bool CKademlia::IsRunning()
{
	return m_bRunning;
}

bool CKademlia::FindNodeIDByIP(CKadClientSearcher& rRequester, uint32 dwIP, uint16 nTCPPort, uint16 nUDPPort) {
	if (!IsRunning() || m_pInstance == NULL || GetUDPListener() == NULL || GetRoutingZone() == NULL){
		ASSERT( false );
		return false;
	}
	// first search our known contacts if we can deliver a result without asking, otherwise forward the request
	CContact* pContact;
	if ((pContact = GetRoutingZone()->GetContact(ntohl(dwIP), nTCPPort, true)) != NULL){
		uchar uchID[16];
		pContact->GetClientID().ToByteArray(uchID);
		rRequester.KadSearchNodeIDByIPResult(KCSR_SUCCEEDED, uchID);
		return true;
	}
	else
		return GetUDPListener()->FindNodeIDByIP(&rRequester, ntohl(dwIP), nTCPPort, nUDPPort);
}

bool CKademlia::FindIPByNodeID(CKadClientSearcher& rRequester, const uchar* pachNodeID){
	if (!IsRunning() || m_pInstance == NULL || GetUDPListener() == NULL){
		ASSERT( false );
		return false;
	}
	// first search our known contacts if we can deliver a result without asking, otherwise forward the request
	CContact* pContact;
	if ((pContact = GetRoutingZone()->GetContact(CUInt128(pachNodeID))) != NULL){
		// make sure that this entry is not too old, otherwise just do a search to be sure
		if (pContact->GetLastSeen() != 0 && time(NULL) - pContact->GetLastSeen() < 1800){
			rRequester.KadSearchIPByNodeIDResult(KCSR_SUCCEEDED, ntohl(pContact->GetIPAddress()), pContact->GetTCPPort());
			return true;
		}
	}
	return CSearchManager::FindNodeSpecial(CUInt128(pachNodeID), &rRequester);
}

void CKademlia::CancelClientSearch(CKadClientSearcher& rFromRequester){
	if (m_pInstance == NULL || GetUDPListener() == NULL){
		ASSERT( false );
		return;
	}
	GetUDPListener()->ExpireClientSearch(&rFromRequester);
	CSearchManager::CancelNodeSpecial(&rFromRequester);
}

void KadGetKeywordHash(const CStringA& rstrKeywordA, Kademlia::CUInt128* pKadID)
{
	CMD4 md4;
	md4.Add((byte*)(LPCSTR)rstrKeywordA, rstrKeywordA.GetLength());
	md4.Finish();
	pKadID->SetValueBE(md4.GetHash());
}

CStringA KadGetKeywordBytes(const Kademlia::CKadTagValueString& rstrKeywordW)
{
	return CStringA(wc2utf8(rstrKeywordW));
}

void KadGetKeywordHash(const Kademlia::CKadTagValueString& rstrKeywordW, Kademlia::CUInt128* pKadID)
{
	KadGetKeywordHash(KadGetKeywordBytes(rstrKeywordW), pKadID);
}

void CKademlia::StatsAddClosestDistance(CUInt128 uDist){
	if (uDist.Get32BitChunk(0) > 0){
		uint32 nToAdd = (0xFFFFFFFF / uDist.Get32BitChunk(0)) / 2;
		if (m_liStatsEstUsersProbes.Find(nToAdd) == NULL)
			m_liStatsEstUsersProbes.AddHead(nToAdd);
	}
	if (m_liStatsEstUsersProbes.GetCount() > 100)
		m_liStatsEstUsersProbes.RemoveTail();
}

uint32 CKademlia::CalculateKadUsersNew(){
	// the idea of calculating the user count with this method is simple:
	// whenever we do search for any NodeID (except in certain cases were the result is not usable),
	// we remember the distance of the closest node we found. Because we assume all NodeIDs are distributed
	// equally, we can calcualte based on this distance how "filled" the possible NodesID room is and by this
	// calculate how many users there are. Of course this only works if we have enough samples, because
	// each single sample will be wrong, but the average of them should produce a usable number. To avoid
	// drifts caused by a a single (or more) really close or really far away hits, we do use median-average instead through

	// doesnt works well if we have no files to index and nothing to download and the numbers seems to be a bit too low
	// compared to out other method. So lets stay with the old one for now, but keeps this here as alternative

	if (m_liStatsEstUsersProbes.GetCount() < 10)
		return 0;
	uint32 nMedian = 0;

	CList<uint32, uint32> liMedian;
	for (POSITION pos1 = m_liStatsEstUsersProbes.GetHeadPosition(); pos1 != NULL; )
	{
		uint32 nProbe = m_liStatsEstUsersProbes.GetNext(pos1);
		bool bInserted = false;
		for (POSITION pos2 = liMedian.GetHeadPosition(); pos2 != NULL; liMedian.GetNext(pos2)){
			if (liMedian.GetAt(pos2) > nProbe){
				liMedian.InsertBefore(pos2, nProbe);
				bInserted = true;
				break;
			}
		}
		if (!bInserted)
			liMedian.AddTail(nProbe);
	}
	// cut away 1/3 of the values - 1/6 of the top and 1/6 of the bottom  to avoid spikes having too much influence, build the average of the rest 
	sint32 nCut = liMedian.GetCount() / 6;
	for (int i = 0; i != nCut; i++){
		liMedian.RemoveHead();
		liMedian.RemoveTail();
	}
	uint64 nAverage = 0;
	for (POSITION pos1 = liMedian.GetHeadPosition(); pos1 != NULL; )
		nAverage += liMedian.GetNext(pos1);
	nMedian = (uint32)(nAverage / liMedian.GetCount());

	// LowIDModififier
	// Modify count by assuming 20% of the users are firewalled and can't be a contact for < 0.49b nodes
	// Modify count by actual statistics of Firewalled ratio for >= 0.49b if we are not firewalled ourself
	// Modify count by 40% for >= 0.49b if we are firewalled outself (the actual Firewalled count at this date on kad is 35-55%)
	const float fFirewalledModifyOld = 1.20F;
	float fFirewalledModifyNew = 0;
	if (CUDPFirewallTester::IsFirewalledUDP(true))
		fFirewalledModifyNew = 1.40F; // we are firewalled and get get the real statistic, assume 40% firewalled >=0.49b nodes
	else if (GetPrefs()->StatsGetFirewalledRatio(true) > 0) {
		fFirewalledModifyNew = 1.0F + (CKademlia::GetPrefs()->StatsGetFirewalledRatio(true)); // apply the firewalled ratio to the modify
		ASSERT( fFirewalledModifyNew > 1.0F && fFirewalledModifyNew < 1.90F );
	}
	float fNewRatio = CKademlia::GetPrefs()->StatsGetKadV8Ratio();
	float fFirewalledModifyTotal = 0;
	if (fNewRatio > 0 && fFirewalledModifyNew > 0) // weigth the old and the new modifier based on how many new contacts we have
		fFirewalledModifyTotal = (fNewRatio * fFirewalledModifyNew) + ((1 - fNewRatio) * fFirewalledModifyOld); 
	else
		fFirewalledModifyTotal = fFirewalledModifyOld;
	ASSERT( fFirewalledModifyTotal > 1.0F && fFirewalledModifyTotal < 1.90F );

	return (uint32)((float)nMedian*fFirewalledModifyTotal);
}

bool CKademlia::LostConnection()
{
	//  根本没启动过Kad，所以也没断开连接的说法
	if(!m_pInstance || !m_pInstance->m_pPrefs)
		return false;

	//  5分钟都没收到任何数据，网络断了，应该重新开始
	if(m_tRecvData && time(NULL)-m_tRecvData > 60 * 5)
		return true;
	return false;
}

time_t CKademlia::m_tNextUpdateWholeKBucket = 0;
void CKademlia::Process_UpdateWholeKBucket()
{
	time_t tNow = time(NULL);

	if (0 == m_tNextUpdateWholeKBucket
		|| tNow > m_tNextUpdateWholeKBucket)
	{
		m_tNextUpdateWholeKBucket = tNow + HR2S(2);

		UpdateWholeKBucket();
	}
}

void CKademlia::UpdateWholeKBucket()
{
	int i;
	for (i = 0; i < (1 << (KBASE)); i++)
	{
		CUInt128	uDistance((ULONG)i);
		uDistance.ShiftLeft(128 - KBASE);
		CUInt128 uRandom(uDistance, 128 - KBASE);
		uRandom.Xor(CKademlia::GetPrefs()->GetKadID());
		CSearchManager::FindNode(uRandom, false);
	}
	for (i = 0; i < (128 - KBASE); i++)
	{
		CUInt128	uDistance((ULONG)1);
		uDistance.ShiftLeft(i);
		CUInt128 uRandom(uDistance, 128 - i);
		uRandom.Xor(CKademlia::GetPrefs()->GetKadID());
		CSearchManager::FindNode(uRandom, false);
	}
}
