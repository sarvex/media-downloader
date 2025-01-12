/*
 *
 *  Copyright (c) 2022
 *  name : Francis Banyikwa
 *  email: mhogomchungu@gmail.com
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "versionInfo.h"
#include "context.hpp"
#include "tabmanager.h"
#include "mainwindow.h"

versionInfo::versionInfo( Ui::MainWindow&,const Context& ctx ) :
	m_ctx( ctx ),
	m_network( m_ctx.network() ),
	m_checkForUpdates( m_ctx.Settings().checkForUpdates() )
{
}

void versionInfo::checkEnginesUpdates( const std::vector< engines::engine >& engines ) const
{
	class meaw : public versionInfo::idone
	{
	public:
		meaw( const Context& t ) : m_ctx( t )
		{
		}
		void operator()() override
		{
			m_ctx.TabManager().init_done() ;
		}
	private:
		const Context& m_ctx ;
	} ;

	this->check( { { engines,utility::sequentialID() },
		     { m_ctx.Settings().showVersionInfoWhenStarting(),false },
		     { util::types::type_identity< meaw >(),m_ctx } } ) ;
}

void versionInfo::log( const QString& msg,int id ) const
{
	m_ctx.logger().add( msg,id ) ;
}

void versionInfo::done( versionInfo::extensionVersionInfo vInfo ) const
{
	QStringList m ;

	QStringList mm ;

	QString s = "%1: %2\n%3: %4\n%5: %6\n" ;

	auto nt = QObject::tr( "Engine Name" ) ;
	auto it = QObject::tr( "Installed Version" ) ;
	auto lt = QObject::tr( "Latest Version" ) ;

	vInfo.report( [ & ]( const QString& name,const QString& iv,const QString& lv ){

		mm.append( name ) ;
		m.append( s.arg( nt,name,it,iv,lt,lv ) ) ;
	} ) ;

	if( m.size() ){

		m_ctx.mainWindow().setTitle( QObject::tr( "There Is An Update For " ) + mm.join( ", " ) ) ;

		auto s = QObject::tr( "Update Found" ) ;

		m_ctx.logger().add( s + "\n" + m.join( "\n" ),utility::sequentialID() ) ;
	}
}

void versionInfo::done( printVinfo vinfo ) const
{
	if( vinfo.hasNext() ){

		this->check( vinfo.next() ) ;
	}else{
		vinfo.reportDone() ;
	}
}

void versionInfo::checkForEnginesUpdates( versionInfo::extensionVersionInfo vInfo,
					  const utils::network::reply& reply ) const
{
	auto lv = vInfo.engine().versionInfoFromGithub( utility::networkReply( m_ctx,reply ).data() ) ;

	if( !lv.version.valid() ){

		if( vInfo.hasNext() ){

			return this->checkForEnginesUpdates( vInfo.next() ) ;
		}else{
			return this->done( vInfo.move() ) ;
		}
	}

	const auto& engine = vInfo.engine() ;

	engines::engine::exeArgs::cmd cmd( engine.exePath(),{ engine.versionArgument() } ) ;

	auto mm = QProcess::ProcessChannelMode::MergedChannels ;

	utility::setPermissions( cmd.exe() ) ;

	utils::qprocess::run( cmd.exe(),cmd.args(),mm,[ lv = std::move( lv ),this,vInfo = vInfo.move() ]( const utils::qprocess::outPut& r ){

		if( r.success() ){

			const auto& engine = vInfo.engine() ;

			util::version iv = engine.setVersionString( r.stdOut ) ;

			auto infov = vInfo.move() ;

			infov.append( engine.name(),std::move( iv ),std::move( lv.version ) ) ;

			if( infov.hasNext() ){

				return this->checkForEnginesUpdates( infov.next() ) ;
			}else{
				this->done( infov.move() ) ;
			}
		}else{
			if( vInfo.hasNext() ){

				return this->checkForEnginesUpdates( vInfo.next() ) ;
			}else{
				this->done( vInfo.move() ) ;
			}
		}
	} ) ;
}

void versionInfo::checkForEnginesUpdates( versionInfo::extensionVersionInfo vInfo ) const
{
	const auto& engine = vInfo.engine() ;

	const auto& url = engine.downloadUrl() ;

	if( url.isEmpty() ){

		if( vInfo.hasNext() ){

			return this->checkForEnginesUpdates( vInfo.next() ) ;
		}else{
			return this->done( std::move( vInfo ) ) ;
		}
	}

	if( engine.name().contains( "yt-dlp" ) && engine.name() != "yt-dlp" ){

		if( vInfo.hasNext() ){

			return this->checkForEnginesUpdates( vInfo.next() ) ;
		}else{
			return this->done( std::move( vInfo ) ) ;
		}
	}

	m_network.get( url,[ this,vInfo = vInfo.move() ]( const utils::network::reply& reply ){

		this->checkForEnginesUpdates( vInfo.move(),reply ) ;
	} ) ;
}

void versionInfo::check( versionInfo::printVinfo vinfo ) const
{
	const auto& engine = vinfo.engine() ;

	auto m = vinfo.setAfterDownloading( false ) ;

	if( engine.usingPrivateBackend() && engine.validDownloadUrl() && networkAccess::hasNetworkSupport() ){

		if( engine.backendExists() ){

			if( m || vinfo.show() ){

				this->printEngineVersionInfo( std::move( vinfo ) ) ;
			}else{
				this->done( std::move( vinfo ) ) ;
			}
		}else{
			auto ee = vinfo.showVersionInfo() ;

			m_network.download( this->wrap( std::move( vinfo ) ),ee ) ;
		}
	}else{
		if( engine.backendExists() ){

			if( vinfo.show() || m ){

				this->printEngineVersionInfo( std::move( vinfo ) ) ;
			}else{
				this->done( std::move( vinfo ) ) ;
			}
		}else{
			if( vinfo.show() ){

				auto m = QObject::tr( "Failed to find version information, make sure \"%1\" is installed and works properly" ).arg( engine.name() ) ;

				this->log( m,vinfo.iter().id() ) ;
			}else{
				this->done( std::move( vinfo ) ) ;
			}
		}
	}
}

void versionInfo::checkForUpdates() const
{
	auto url = "https://api.github.com/repos/mhogomchungu/media-downloader/releases/latest" ;

	m_network.get( url,[ this ]( const utils::network::reply& reply ){

		utility::networkReply nreply( m_ctx,reply ) ;

		if( reply.success() ){

			QJsonParseError err ;

			auto e = QJsonDocument::fromJson( nreply.data(),&err ) ;

			if( err.error == QJsonParseError::NoError ){

				auto lv = e.object().value( "tag_name" ).toString() ;
				auto iv = utility::runningVersionOfMediaDownloader() ;

				versionInfo::extensionVersionInfo vInfo = m_ctx.Engines().getEnginesIterator() ;

				vInfo.append( m_ctx.appName(),iv,lv ) ;

				this->checkForEnginesUpdates( std::move( vInfo ) ) ;
			}else{
				m_ctx.logger().add( err.errorString(),utility::sequentialID() ) ;

				this->checkForEnginesUpdates( m_ctx.Engines().getEnginesIterator() ) ;
			}
		}
	} ) ;
}

void versionInfo::checkMediaDownloaderUpdate( int id,
					      const QByteArray& data,
					      const std::vector< engines::engine >& engines ) const
{
	QJsonParseError err ;

	auto e = QJsonDocument::fromJson( data,&err ) ;

	if( err.error == QJsonParseError::NoError ){

		auto lvs = e.object().value( "tag_name" ).toString() ;

		util::version lv = lvs  ;
		util::version iv = utility::runningVersionOfMediaDownloader() ;

		versionInfo::extensionVersionInfo vInfo = m_ctx.Engines().getEnginesIterator() ;

		vInfo.append( m_ctx.appName(),iv,lv ) ;

		if( lv.valid() && iv < lv ){

			this->log( QObject::tr( "Newest Version Is %1, Updating" ).arg( lvs ),id ) ;

			class meaw : public networkAccess::status
			{
			public:
				meaw( const std::vector< engines::engine >& m,const versionInfo& v ) :
					m_engines( m ),
					m_parent( v )
				{
				}
				void done()
				{
					m_parent.checkEnginesUpdates( m_engines ) ;
				}
			private:
				const std::vector< engines::engine >& m_engines ;
				const versionInfo& m_parent ;
			} ;

			networkAccess::Status s{ util::types::type_identity< meaw >(),engines,*this } ;

			m_network.updateMediaDownloader( id,std::move( s ) ) ;
		}else{
			this->checkEnginesUpdates( engines ) ;
		}
	}else{
		m_ctx.logger().add( err.errorString(),id ) ;

		this->checkEnginesUpdates( engines ) ;
	}
}

void versionInfo::checkMediaDownloaderUpdate( const std::vector< engines::engine >& engines ) const
{
	if( !m_ctx.Settings().showVersionInfoWhenStarting() ){

		return this->checkEnginesUpdates( engines ) ;
	}

	m_ctx.TabManager().disableAll() ;

	auto id = utility::sequentialID() ;

	this->log( QObject::tr( "Checking installed version of %1" ).arg( m_ctx.appName() ),id ) ;

	this->log( QObject::tr( "Found version: %1" ).arg( utility::runningVersionOfMediaDownloader() ),id ) ;

	if( !m_checkForUpdates ){

		return this->checkEnginesUpdates( engines ) ;
	}

	auto url = "https://api.github.com/repos/mhogomchungu/media-downloader/releases/latest" ;

	m_network.get( url,[ this,id,&engines ]( const utils::network::reply& reply ){

		utility::networkReply nreply( m_ctx,reply ) ;

		if( reply.success() ){

			this->checkMediaDownloaderUpdate( id,nreply.data(),engines ) ;
		}else{
			m_ctx.TabManager().enableAll() ;
		}
	} ) ;
}

networkAccess::iterator versionInfo::wrap( printVinfo m ) const
{
	class meaw : public networkAccess::iter
	{
	public:
		meaw( printVinfo m ) : m_vInfo( std::move( m ) )
		{
		}
		const engines::engine& engine() override
		{
			return m_vInfo.engine() ;
		}
		bool hasNext() override
		{
			return m_vInfo.hasNext() ;
		}
		void moveToNext() override
		{
			m_vInfo = m_vInfo.next() ;
		}
		void reportDone() override
		{
			m_vInfo.reportDone() ;
		}
		void failed() override
		{
			m_vInfo.failed() ;
		}
		const engines::Iterator& itr() override
		{
			return m_vInfo.iter() ;
		}
	private:
		printVinfo m_vInfo ;
	};

	return { util::types::type_identity< meaw >(),std::move( m ) } ;
}

void versionInfo::printEngineVersionInfo( versionInfo::printVinfo vInfo ) const
{
	m_ctx.TabManager().disableAll() ;

	const auto& engine = vInfo.engine() ;

	auto id = utility::sequentialID() ;

	this->log( QObject::tr( "Checking installed version of %1" ).arg( engine.name() ),id ) ;

	if( engine.name().contains( "yt-dlp" ) && engine.name() != "yt-dlp" ){

		const auto& e = m_ctx.Engines().getEngineByName( "yt-dlp" ) ;

		if( e.has_value() ){

			const auto& version = e.value().versionInfo() ;

			if( version.valid() ){

				this->log( QObject::tr( "Found version: %1" ).arg( version.toString() ),id ) ;

				return this->done( std::move( vInfo ) ) ;
			}
		}
	}

	engines::engine::exeArgs::cmd cmd( engine.exePath(),{ engine.versionArgument() } ) ;

	if( !m_ctx.debug().isEmpty() ){

		auto exe = "cmd: \"" + cmd.exe() + "\"" ;

		for( const auto& it : cmd.args() ){

			exe += " \"" + it + "\"" ;
		}

		m_ctx.logger().add( exe,id ) ;
	}

	auto mm = QProcess::ProcessChannelMode::MergedChannels ;

	utility::setPermissions( cmd.exe() ) ;

	utils::qprocess::run( cmd.exe(),cmd.args(),mm,[ this,id,vInfo = vInfo.move() ]( const utils::qprocess::outPut& r ){

		this->printEngineVersionInfo( vInfo.move(),id,r ) ;
	} ) ;
}

void versionInfo::printEngineVersionInfo( versionInfo::printVinfo vInfo,
					  int id,
					  const utils::qprocess::outPut& r ) const
{
	const auto& engine = vInfo.engine() ;

	if( r.success() ){

		this->log( QObject::tr( "Found version: %1" ).arg( engine.setVersionString( r.stdOut ) ),id ) ;

		const auto& url = engine.downloadUrl() ;

		if( !url.isEmpty() && m_checkForUpdates ){

			m_network.get( url,[ id,this,vInfo = vInfo.move() ]( const utils::network::reply& reply ){

				this->printEngineVersionInfo( vInfo.move(),id,reply ) ;
			} ) ;

			return ;
		}else{
			m_ctx.TabManager().enableAll() ;
		}
	}else{
		this->log( QObject::tr( "Failed to find version information, make sure \"%1\" is installed and works properly" ).arg( engine.name() ),id ) ;

		m_ctx.TabManager().enableAll() ;

		engine.setBroken() ;
	}

	this->done( vInfo.move() ) ;
}

void versionInfo::printEngineVersionInfo( versionInfo::printVinfo vInfo,
					  int id,
					  const utils::network::reply& reply ) const
{
	const auto& engine = vInfo.engine() ;

	const auto& versionOnline = engine.versionInfoFromGithub( utility::networkReply( m_ctx,reply ).data() ) ;
	const auto& installedVersion = engine.versionInfo() ;

	const auto& version = versionOnline.version ;

	if( version.valid() && installedVersion.valid() && installedVersion < version ){

		auto m = versionOnline.stringVersion ;

		this->log( QObject::tr( "Newest Version Is %1, Updating" ).arg( m ) ,id ) ;

		auto ee = vInfo.showVersionInfo() ;

		m_network.download( this->wrap( vInfo.move() ),ee ) ;
	}else{
		m_ctx.TabManager().enableAll() ;

		this->done( vInfo.move() ) ;
	}
}

versionInfo::idone::~idone()
{
}
