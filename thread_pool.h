#include <iostream>
#include <thread>
#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stdexcept>
#include <memory>

namespace TP
{
    class CPool
    {
    public:
        CPool( size_t iNumThreads = std::thread::hardware_concurrency( ) )
        {
            if ( iNumThreads == 0 )
                throw std::invalid_argument( "Number of threads cannot be zero" );

            for ( size_t i = 0; i < iNumThreads; ++i )
                m_Threads.emplace_back( [ this ] { WorkLoop( ); } );
        }

        ~CPool( )
        {
            Stop( );
        }

        template <typename Fn, typename... Args>
        auto Enqueue( Fn&& fn, Args&&... args ) -> std::future<std::invoke_result_t<Fn, Args...>>
        {
            using ReturnType = std::invoke_result_t<Fn, Args...>;

            auto Task = std::make_shared<std::packaged_task<ReturnType( )>>(
                std::bind( std::forward<Fn>( fn ), std::forward<Args>( args )... )
            );

            auto Future = Task->get_future( );

            {
                std::unique_lock Lock{ m_QueueMutex };

                if ( m_bStop )
                    throw std::runtime_error( "Thread pool is stopped" );

                m_Tasks.emplace( [ Task ] ( ) { ( *Task )( ); } );
            }

            m_Condition.notify_one( );

            return Future;
        }

        void Wait( )
        {
            std::unique_lock Lock{ m_QueueMutex };

            m_Condition.wait( Lock, [ this ] ( ) {
                return m_Tasks.empty( ) && ( m_iBusyThreads == 0 );
            } );
        }

        void Stop( )
        {
            {
                std::unique_lock Lock{ m_QueueMutex };
                m_bStop = true;
            }

            m_Condition.notify_all( );

            for ( auto& Thread : m_Threads )
            {
                if ( Thread.joinable( ) )
                    Thread.join( );
            }
        }

    private:
        void WorkLoop( )
        {
            while ( true )
            {
                std::function<void( )> Task;

                {
                    std::unique_lock Lock{ m_QueueMutex };

                    m_Condition.wait( Lock, [ this ] ( ) {
                        return !m_Tasks.empty( ) || m_bStop;
                    } );

                    if ( m_bStop && m_Tasks.empty( ) )
                        return;

                    Task = std::move( m_Tasks.front( ) );
                    m_Tasks.pop( );

                    ++m_iBusyThreads;
                }

                Task( );

                {
                    std::unique_lock Lock{ m_QueueMutex };

                    --m_iBusyThreads;

                    if ( m_iBusyThreads == 0 && m_Tasks.empty( ) )
                        m_Condition.notify_all( );
                }
            }
        }

        std::vector<std::thread>            m_Threads{ };
        std::queue<std::function<void( )>>  m_Tasks{ };
        std::mutex                          m_QueueMutex{ };
        std::condition_variable             m_Condition{ };
        size_t                              m_iBusyThreads{ };
        bool                                m_bStop{ };
    };
}
