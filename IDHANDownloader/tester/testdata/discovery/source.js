export const manifest = {
    id: "test.discovery-pixiv",
    version: 1,
    flags: [
        {
            name: "pixiv.mock",
            description: "Verify flags for a followed parser script.",
            default: false,
        },
    ],
};

export function post(input) {
    if (!input.flags["pixiv.mock"]) {
        throw new Error("Expected the followed parser to receive pixiv.mock=true");
    }

    return {complete: true};
}
