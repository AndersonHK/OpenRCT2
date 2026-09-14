# Both install and macOS bundle paths consume the same pinned source contract.
function(install_fork_objects root source destination)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${root}/scripts/install-fork-objects.py"
            --manifest "${root}/assets.json" --source "${source}" --destination "${destination}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Installing companion fork objects failed (${result})")
    endif()
endfunction()
